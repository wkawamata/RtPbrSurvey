"""Bounded RAW integration checks for smooth thin-transmission textures."""

import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np

from transmission_texture_fixture import BACKGROUND, COLOR_RGBA, RATE_RGBA, ROOT, write_fixture
from validate_inputs import read_buffer
from validate_part1 import sha, write_json

FALLBACK = "Native fallback: thin transmission is not supported by NRD"


def srgb_to_linear(rgb):
    rgb = np.asarray(rgb, dtype=np.float64) / 255.0
    return np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)


def fresnel(cosine, ior):
    cosine = np.asarray(cosine, dtype=np.float64)
    if ior == 1:
        return np.zeros_like(cosine)
    transmitted = np.sqrt(1 - (1 - cosine * cosine) / (ior * ior))
    parallel = (ior * cosine - transmitted) / (ior * cosine + transmitted)
    perpendicular = (cosine - ior * transmitted) / (cosine + ior * transmitted)
    return (parallel * parallel + perpendicular * perpendicular) * 0.5


def mean_gate(actual, expected_mean, samples, amplitude, deterministic=False):
    if actual.ndim != 3 or actual.shape[2] < 3 or not actual.size or not np.isfinite(actual).all():
        raise ValueError("Invalid or nonfinite RGB patch")
    if samples < 1:
        raise ValueError("Missing samples")
    expected_mean = np.asarray(expected_mean, dtype=np.float64)
    amplitude = np.asarray(amplitude, dtype=np.float64)
    if (expected_mean.shape != (3,) or not np.isfinite(expected_mean).all() or amplitude.shape != (3,)
            or not np.isfinite(amplitude).all() or np.any(amplitude < 0)):
        raise ValueError("Invalid reference")
    count = actual.shape[0] * actual.shape[1] * samples
    tolerance = np.full(3, 0.001) if deterministic else 0.001 + 6 * amplitude / (2 * math.sqrt(count))
    error = np.abs(actual[..., :3].mean(axis=(0, 1)) - expected_mean)
    if np.any(error > tolerance):
        raise ValueError(f"RGB mean mismatch: error={error.tolist()}, tolerance={tolerance.tolist()}")
    if deterministic and np.max(np.abs(actual[..., :3] - expected_mean)) > 0.001:
        raise ValueError("Deterministic texture sample mismatch")
    return dict(mean=actual[..., :3].mean(axis=(0, 1)).tolist(), expected=expected_mean.tolist(),
                error=error.tolist(), tolerance=tolerance.tolist(), sampleBudget=count)


def assess(image, frames, rate, tint, factor=1.0, ior=1.5, shared=False):
    if image.ndim != 3 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid captured image")
    height, width = image.shape[:2]
    focal = height / (2 * math.tan(math.radians(40) / 2))
    half = max(4, round(min(height, width) * 0.012))
    checks = []
    for tile in range(4):
        u, v = 0.25 + (tile % 2) * 0.5, 0.25 + (tile // 2) * 0.5
        x = round(width * 0.5 + (u - 0.5) * 2.8 * focal / 5)
        y = round(height * 0.5 - (v - 0.5) * 2.4 * focal / 5)
        roi = [x - half, y - half, x + half, y + half]
        patch = image[roi[1]:roi[3], roi[0]:roi[2], :3]
        yy, xx = np.mgrid[roi[1]:roi[3], roi[0]:roi[2]]
        cosine = 1 / np.sqrt(1 + ((xx + 0.5 - width * 0.5) / focal) ** 2
                            + ((height * 0.5 - yy - 0.5) / focal) ** 2)
        probability = factor * (RATE_RGBA[tile][0] / 255 if rate else 1)
        color = srgb_to_linear((RATE_RGBA if shared else COLOR_RGBA)[tile][:3]) if tint else np.ones(3)
        color *= np.array([0.6, 0.8, 1.0])
        amplitude = np.array(BACKGROUND) * color
        expected = amplitude * probability * (1 - fresnel(cosine, ior)).mean()
        result = mean_gate(patch, expected, frames, amplitude, deterministic=ior == 1 and probability == 1)
        result.update(tile=tile, uv=[u, v], roi=roi, probability=probability)
        checks.append(result)
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=64)
    args = parser.parse_args()
    if not 16 <= args.frames <= 512:
        parser.error("--frames must be in [16, 512]")
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = {"color": dict(rate=False, tint=True, ior=1.0),
             "rate": dict(rate=True, tint=False),
             "scaled-rate": dict(rate=True, tint=False, factor=0.5),
             "combined": dict(rate=True, tint=True),
             "shared-image": dict(rate=True, tint=True, shared=True),
             "nrd-fallback": dict(rate=True, tint=True)}
    report = dict(status="running", scope="smooth-thin-transmission-textures", qualityVerdict="not-evaluated",
                  executableSha256=sha(exe), runnerSha256=sha(Path(__file__)),
                  fixtureGeneratorSha256=sha(Path(__file__).with_name("transmission_texture_fixture.py")),
                  frames=args.frames, expectedCases=list(cases), cases=[], failures=[])
    combined = None
    write_json(output / "report.json", report)
    try:
        for name, config in cases.items():
            folder = output / name
            write_fixture(folder, **config)
            capture = folder / "SceneColor.ptbuf"
            log = folder / "debug.log"
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
                       "-DebugPreviewResource", "PathTracing.SceneColor", "-CapturePath", str(capture),
                       "-CaptureAfterFrames", str(args.frames), "-LogToFile", str(log), "-ExitAfterCapture"]
            nrd = name == "nrd-fallback"
            if nrd:
                command.append("-NrdPreview")
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=120)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                raise ValueError("Capture timeout: " + name)
            text = log.read_text(encoding="utf-8-sig")
            if code or "[ERROR]" in text or "[CORRUPTION]" in text:
                raise ValueError("Runtime/Debug Layer failure: " + name)
            records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                       if line.startswith("[PathTracing] ")]
            if len(records) != 1:
                raise ValueError("Missing unique PT runtime record: " + name)
            state = records[0]
            meta, image = read_buffer(capture)
            if (meta.get("resource") != "PathTracing.SceneColor" or meta.get("thinTransmissionActive") is not True
                    or meta.get("sampleStartIndex") != args.frames or meta.get("samplesPerFrame") != 1
                    or meta.get("primarySampling") != "pixel-center"
                    or state.get("accumulatedSamples") != args.frames or state.get("maxBounces") != 8
                    or state.get("pixelCenterPrimary") is not True or state.get("environmentEnabled") is not False
                    or state.get("directLightingEnabled") is not False or state.get("d3d12DebugLayerEnabledByApp") is not True):
                raise ValueError("Capture state differs from the declared fixture: " + name)
            if nrd and (meta.get("nrdPresentation") != FALLBACK
                        or meta.get("nrdLastEvaluation", {}).get("recorded") is not False):
                raise ValueError("Textured thin material did not bypass NRD")
            checks = assess(image, args.frames, **config)
            if name == "combined":
                combined = image.copy()
            if nrd and (combined is None or not np.array_equal(image, combined)):
                raise ValueError("NRD fallback changed Native PT output")
            inputs = {path.name: sha(path) for path in folder.iterdir()
                      if path.suffix in (".json", ".gltf", ".png")}
            report["cases"].append(dict(case=name, config=config, command=command, metadata=meta, runtime=state,
                                        inputSha256=inputs, bufferSha256=sha(capture), logSha256=sha(log), checks=checks))
            write_json(output / "report.json", report)
        if [case["case"] for case in report["cases"]] != list(cases):
            raise ValueError("Missing, duplicated or reordered texture cases")
        report["status"] = "passed-thin-transmission-textures"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(json.dumps(dict(status=report["status"], cases=len(report["cases"]))))


if __name__ == "__main__":
    main()
