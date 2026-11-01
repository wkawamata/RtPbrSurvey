"""White-furnace runtime check for the single-scattering GGX thin-sheet closure."""

import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from transmission_texture_fixture import ROOT, write_fixture, write_json
from validate_inputs import read_buffer
from validate_part1 import sha


def furnace_reference(roughness, steps=65536):
    if roughness == 0:
        return 1.0
    alpha = max(roughness * roughness, 0.001)
    cosine = (np.arange(steps, dtype=np.float64) + 0.5) / steps
    denominator = 1 + (alpha * alpha - 1) * (1 + cosine) * 0.5
    masking = 2 * cosine / (cosine + np.sqrt(alpha * alpha + (1 - alpha * alpha) * cosine * cosine))
    return float(np.mean(alpha * alpha * masking / (2 * denominator * denominator)))


def assess_furnace(image, roughness, frames):
    if image.ndim != 3 or image.shape[2] < 3 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Missing or nonfinite furnace image")
    height, width = image.shape[:2]
    roi = [int(width * .49), int(height * .49), int(width * .51), int(height * .51)]
    patch = image[roi[1]:roi[3], roi[0]:roi[2], :3]
    expected = furnace_reference(roughness)
    tolerance = .001 if roughness == 0 else .006 + 4.2 / np.sqrt(patch.shape[0] * patch.shape[1] * frames)
    error = np.abs(patch.mean(axis=(0, 1)) - expected)
    if frames < 1 or np.any(error > tolerance):
        raise ValueError(f"Furnace mismatch: {error.tolist()}, budget={tolerance}")
    return dict(roi=roi, expected=expected, mean=patch.mean(axis=(0, 1)).tolist(),
                error=error.tolist(), tolerance=float(tolerance)), patch


def write_furnace_fixture(folder, roughness, mode):
    write_fixture(folder, rate=False, tint=False)
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    gltf["materials"] = gltf["materials"][:1]
    gltf["meshes"] = gltf["meshes"][:1]
    gltf["nodes"] = gltf["nodes"][:1]
    gltf["scenes"][0]["nodes"] = [0]
    gltf["materials"][0]["pbrMetallicRoughness"].update(baseColorFactor=[1, 1, 1, 1], roughnessFactor=roughness)
    write_json(folder / "panes.gltf", gltf)
    preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
    preset["lighting"]["iblIntensity"] = 1
    preset["backBufferClearColor"] = [1, 1, 1, 1]
    preset["pathTracing"].update(environmentEnabled=True, environmentSamplingMode=mode)
    write_json(folder / "render-preset.json", preset)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=128)
    args = parser.parse_args()
    if not 64 <= args.frames <= 512:
        parser.error("frames must be in [64, 512]")
    exe = args.exe.resolve(strict=True)
    shader = (exe.parent / "shaders_PathTracing_CSMain.cso").resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = [("smooth", 0, 2), ("rough-mid", .5, 2), ("rough-high", 1, 2), ("rough-mid-bsdf", .5, 5)]
    report = dict(status="running", executableSha256=sha(exe), runnerSha256=sha(Path(__file__)),
                  pathTracingShaderSha256=sha(shader),
                  generatorSha256=sha(Path(__file__).with_name("transmission_texture_fixture.py")),
                  frames=args.frames, expectedCases=[case[0] for case in cases], cases=[], failures=[])
    mid_patch = None
    try:
        for name, roughness, mode in cases:
            folder = output / name
            write_furnace_fixture(folder, roughness, mode)
            buffer, log = folder / "SceneColor.ptbuf", folder / "debug.log"
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
                       "-DebugPreviewResource", "PathTracing.SceneColor", "-CapturePath", str(buffer),
                       "-CaptureAfterFrames", str(args.frames), "-LogToFile", str(log), "-ExitAfterCapture"]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                raise ValueError("Furnace timeout: " + name)
            text = log.read_text(encoding="utf-8-sig")
            records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                       if line.startswith("[PathTracing] ")]
            if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(records) != 1:
                raise ValueError("Furnace runtime failure: " + name)
            state = records[0]
            meta, image = read_buffer(buffer)
            if (meta.get("resource") != "PathTracing.SceneColor" or meta.get("thinTransmissionActive") is not True
                    or meta.get("sampleStartIndex") != args.frames or meta.get("primarySampling") != "pixel-center"
                    or meta.get("samplesPerFrame") != 1 or state.get("maxBounces") != 8
                    or state.get("pixelCenterPrimary") is not True or state.get("nrdPreviewRequested") is not False
                    or state.get("samplesPerFrame") != 1 or state.get("accumulatedSamples") != args.frames
                    or state.get("environmentEnabled") is not True or state.get("environmentSamplingMode") != mode
                    or state.get("d3d12DebugLayerEnabledByApp") is not True):
                raise ValueError("Furnace runtime state mismatch: " + name)
            assessment, patch = assess_furnace(image, roughness, args.frames)
            if name == "rough-mid":
                mid_patch = patch.copy()
            if name == "rough-mid-bsdf" and (mid_patch is None or not np.array_equal(mid_patch, patch)):
                raise ValueError("Unused environment NEE mode changed the thin BSDF estimator")
            inputs = {path.name: sha(path) for path in folder.iterdir() if path.suffix in (".json", ".gltf", ".png")}
            report["cases"].append(dict(case=name, roughness=roughness, environmentMode=mode,
                                        command=command, runtime=state, metadata=meta, assessment=assessment,
                                        inputSha256=inputs, bufferSha256=sha(buffer), logSha256=sha(log)))
            write_json(output / "report.json", report)
        if [case["case"] for case in report["cases"]] != report["expectedCases"]:
            raise ValueError("Incomplete furnace case list")
        if sha(shader) != report["pathTracingShaderSha256"] or sha(exe) != report["executableSha256"]:
            raise ValueError("Runtime binaries changed during furnace validation")
        report["status"] = "passed-rough-thin-furnace"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
