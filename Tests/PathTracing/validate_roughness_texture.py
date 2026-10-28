"""Validate roughness texture G in RAW guides and the final thin-sheet transport."""

import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np

from roughness_texture_fixture import ROUGHNESS_RGBA, write_roughness_texture_fixture
from transmission_texture_fixture import ROOT, write_json
from validate_inputs import read_buffer
from validate_part1 import sha
from validate_rough_transmission import assess_furnace


def assess_guide(image, factor, varying):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid roughness guide")
    height, width = image.shape[:2]
    focal = height / (2 * math.tan(math.radians(40) / 2))
    half = max(4, round(min(height, width) * .012))
    checks = []
    for tile in range(4):
        u, v = .25 + (tile % 2) * .5, .25 + (tile // 2) * .5
        x = round(width * .5 + (u - .5) * 2.8 * focal / 5)
        y = round(height * .5 - (v - .5) * 2.4 * focal / 5)
        roi = [x - half, y - half, x + half, y + half]
        patch = image[roi[1]:roi[3], roi[0]:roi[2], 3]
        expected = factor * (ROUGHNESS_RGBA[tile][1] if varying else 128) / 255
        error = float(np.max(np.abs(patch - expected)))
        if error > .001:
            raise ValueError(f"Roughness G mismatch in tile {tile}: {error}")
        checks.append(dict(tile=tile, roi=roi, expected=expected, mean=float(patch.mean()),
                           maxAbsoluteError=error, tolerance=.001))
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
    cases = [("guide", 1, False, "NormalRoughness"), ("guide-scaled", .5, False, "NormalRoughness"),
             ("guide-zero", 0, False, "NormalRoughness"), ("guide-varying", .75, True, "NormalRoughness"),
             ("furnace", 1, False, "SceneColor"), ("furnace-scaled", .5, False, "SceneColor")]
    dependencies = ["roughness_texture_fixture.py", "transmission_texture_fixture.py",
                    "rough_transmission_fixture.py", "validate_rough_transmission.py"]
    report = dict(status="running", executableSha256=sha(exe), pathTracingShaderSha256=sha(shader),
                  runnerSha256=sha(Path(__file__)), frames=args.frames,
                  dependencySha256={name: sha(Path(__file__).with_name(name)) for name in dependencies},
                  expectedCases=[case[0] for case in cases], cases=[], failures=[])
    try:
        for name, factor, varying, resource in cases:
            folder = output / name
            write_roughness_texture_fixture(folder, factor=factor, varying=varying)
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
                raise ValueError("Roughness texture capture timeout: " + name)
            text = log.read_text(encoding="utf-8-sig")
            records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                       if line.startswith("[PathTracing] ")]
            if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(records) != 1:
                raise ValueError("Runtime or Debug Layer failure: " + name)
            state = records[0]
            meta, image = read_buffer(buffer)
            if (meta.get("resource") != resource_name or meta.get("thinTransmissionActive") is not True
                    or meta.get("sampleStartIndex") != frames or meta.get("samplesPerFrame") != 1
                    or meta.get("primarySampling") != "pixel-center" or state.get("maxBounces") != 8
                    or state.get("pixelCenterPrimary") is not True or state.get("samplesPerFrame") != 1
                    or state.get("accumulatedSamples") != frames or state.get("environmentEnabled") is not True
                    or state.get("environmentSamplingMode") != 5 or state.get("nrdPreviewRequested") is not False
                    or state.get("d3d12DebugLayerEnabledByApp") is not True):
                raise ValueError("Required capture state mismatch: " + name)
            assessment = (assess_furnace(image, factor * 128 / 255, frames)[0] if resource == "SceneColor"
                          else assess_guide(image, factor, varying))
            inputs = {path.name: sha(path) for path in folder.iterdir() if path.suffix in (".json", ".gltf", ".png")}
            report["cases"].append(dict(case=name, factor=factor, varying=varying, resource=resource_name,
                                        frames=frames, command=command, runtime=state, metadata=meta,
                                        assessment=assessment, inputSha256=inputs,
                                        bufferSha256=sha(buffer), logSha256=sha(log)))
            write_json(output / "report.json", report)
        if [case["case"] for case in report["cases"]] != report["expectedCases"]:
            raise ValueError("Incomplete roughness texture cases")
        if sha(shader) != report["pathTracingShaderSha256"] or sha(exe) != report["executableSha256"]:
            raise ValueError("Runtime binaries changed during validation")
        report["status"] = "passed-roughness-texture"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
