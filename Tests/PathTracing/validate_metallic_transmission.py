"""RAW runtime checks for metallic/thin-sheet mixtures and the opaque endpoint."""

import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np

from metallic_transmission_fixture import METALLIC_RGBA, write_metallic_fixture
from transmission_texture_fixture import BACKGROUND, ROOT, write_json
from validate_inputs import read_buffer
from validate_part1 import sha
from validate_transmission_textures import fresnel


def assess_mixture(image, metallic, frames, backface=False):
    if image.ndim != 3 or image.shape[2] < 3 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid metallic mixture image")
    height, width = image.shape[:2]
    roi = [int(width * .48), int(height * .48), int(width * .52), int(height * .52)]
    patch = image[roi[1]:roi[3], roi[0]:roi[2], :3]
    yy, xx = np.mgrid[roi[1]:roi[3], roi[0]:roi[2]]
    focal = height / (2 * math.tan(math.radians(40) / 2))
    cosine = 1 / np.sqrt(1 + ((xx + .5 - width * .5) / focal) ** 2 + ((height * .5 - yy - .5) / focal) ** 2)
    reflection = float(fresnel(cosine, 1.5).mean())
    dielectric = reflection + (1 - reflection) * np.array(BACKGROUND)
    expected = np.array(BACKGROUND) if backface else metallic + (1 - metallic) * dielectric
    tolerance = .001 if backface or metallic == 1 else .003 + 3.3 / np.sqrt(patch.shape[0] * patch.shape[1] * frames)
    error = np.abs(patch.mean(axis=(0, 1)) - expected)
    if frames < 1 or np.any(error > tolerance):
        raise ValueError(f"Metallic mixture RGB mismatch: {error.tolist()}, budget={tolerance}")
    return dict(roi=roi, expected=expected.tolist(), mean=patch.mean(axis=(0, 1)).tolist(),
                error=error.tolist(), tolerance=float(tolerance)), patch


def assess_metallic_guide(image, factor):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid metallic guide")
    height, width = image.shape[:2]
    focal = height / (2 * math.tan(math.radians(40) / 2))
    half = max(4, round(min(height, width) * .012))
    checks = []
    for tile in range(4):
        u, v = .25 + (tile % 2) * .5, .25 + (tile // 2) * .5
        x = round(width * .5 + (u - .5) * 2.8 * focal / 5)
        y = round(height * .5 - (v - .5) * 2.4 * focal / 5)
        patch = image[y - half:y + half, x - half:x + half, 3]
        expected = factor * METALLIC_RGBA[tile][2] / 255
        error = float(np.max(np.abs(patch - expected)))
        if error > .001:
            raise ValueError("Metallic B/factor guide mismatch")
        checks.append(dict(tile=tile, expected=expected, maxAbsoluteError=error, tolerance=.001))
    return checks


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
    cases = [("dielectric", dict(metallic=0), "SceneColor"), ("mixed", dict(metallic=.5), "SceneColor"),
             ("metal", dict(metallic=1), "SceneColor"), ("opaque-control", dict(metallic=1, transmission=0, opaque=True), "SceneColor"),
             ("textured-mixed", dict(metallic=1, blue=128), "SceneColor"),
             ("textured-dielectric", dict(metallic=1, blue=0), "SceneColor"),
             ("guide", dict(metallic=.5, varying=True), "MaterialF0Metallic"),
             ("metal-backface", dict(metallic=1, backface=True), "SceneColor")]
    dependencies = ["metallic_transmission_fixture.py", "transmission_texture_fixture.py", "validate_transmission_textures.py"]
    report = dict(status="running", executableSha256=sha(exe), pathTracingShaderSha256=sha(shader),
                  runnerSha256=sha(Path(__file__)), frames=args.frames, cases=[], failures=[],
                  expectedCases=[case[0] for case in cases],
                  dependencySha256={name: sha(Path(__file__).with_name(name)) for name in dependencies})
    metal_patch = None
    try:
        for name, config, resource in cases:
            folder = output / name
            write_metallic_fixture(folder, **config)
            frames = args.frames if resource == "SceneColor" else 8
            buffer, log = folder / (resource + ".ptbuf"), folder / "debug.log"
            resource_name = "PathTracing." + resource
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
                       "-DebugPreviewResource", resource_name, "-CapturePath", str(buffer),
                       "-CaptureAfterFrames", str(frames), "-LogToFile", str(log), "-ExitAfterCapture"]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                raise ValueError("Metallic capture timeout: " + name)
            text = log.read_text(encoding="utf-8-sig")
            records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                       if line.startswith("[PathTracing] ")]
            if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(records) != 1:
                raise ValueError("Runtime/Debug Layer failure: " + name)
            state = records[0]
            meta, image = read_buffer(buffer)
            thin_active = config.get("transmission", 1) > 0
            if (meta.get("resource") != resource_name or meta.get("thinTransmissionActive") is not thin_active
                    or meta.get("sampleStartIndex") != frames or meta.get("samplesPerFrame") != 1
                    or meta.get("primarySampling") != "pixel-center" or state.get("maxBounces") != 8
                    or state.get("pixelCenterPrimary") is not True or state.get("samplesPerFrame") != 1
                    or state.get("accumulatedSamples") != frames or state.get("environmentEnabled") is not True
                    or state.get("environmentSamplingMode") != 5 or state.get("nrdPreviewRequested") is not False
                    or state.get("d3d12DebugLayerEnabledByApp") is not True):
                raise ValueError("Required metallic capture state mismatch: " + name)
            if resource == "MaterialF0Metallic":
                assessment = assess_metallic_guide(image, config["metallic"])
            else:
                metallic = config["metallic"] * (config["blue"] / 255 if "blue" in config else 1)
                assessment, patch = assess_mixture(image, metallic, frames, config.get("backface", False))
                if name == "metal":
                    metal_patch = patch.copy()
                if name == "opaque-control" and (metal_patch is None or not np.array_equal(metal_patch, patch)):
                    raise ValueError("Transmission changed the fully metallic endpoint")
            inputs = {path.name: sha(path) for path in folder.iterdir() if path.suffix in (".json", ".gltf", ".png")}
            report["cases"].append(dict(case=name, config=config, resource=resource_name, command=command,
                                        metadata=meta, runtime=state, assessment=assessment, inputSha256=inputs,
                                        bufferSha256=sha(buffer), logSha256=sha(log)))
            write_json(output / "report.json", report)
        if [case["case"] for case in report["cases"]] != report["expectedCases"]:
            raise ValueError("Incomplete metallic case list")
        if sha(exe) != report["executableSha256"] or sha(shader) != report["pathTracingShaderSha256"]:
            raise ValueError("Runtime binaries changed during metallic validation")
        report["status"] = "passed-metallic-transmission"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
