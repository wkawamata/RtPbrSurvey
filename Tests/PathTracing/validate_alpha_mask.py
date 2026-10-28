"""RAW coverage, secondary-ray, shadow and emitter tests for Native PT alpha MASK."""

import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np

from alpha_mask_fixture import ALPHAS, PLATE, write_alpha_fixture, write_visibility_fixture
from transmission_texture_fixture import BACKGROUND, ROOT, write_json
from validate_inputs import read_buffer
from validate_part1 import sha

FALLBACK = "Native fallback: alpha MASK is not qualified for NRD"


def assess_primary(image, config):
    if image.ndim != 3 or image.shape[2] < 3 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid alpha MASK image")
    height, width = image.shape[:2]
    focal = height / (2 * math.tan(math.radians(40) / 2))
    half = max(4, round(min(height, width) * .012))
    checks = []
    for tile in range(4):
        u, v = .25 + (tile % 2) * .5, .25 + (tile // 2) * .5
        x = round(width * .5 + (u - .5) * 2.8 * focal / 5)
        y = round(height * .5 - (v - .5) * 2.4 * focal / 5)
        patch = image[y - half:y + half, x - half:x + half, :3]
        source_tile = tile ^ 1 if config.get("backface") else tile
        alpha = ALPHAS[source_tile] / 255 if config.get("texture", True) else 1
        covered = config.get("mode", "MASK") == "OPAQUE" or alpha * config.get("factor", 1) >= config.get("cutoff", .5)
        if config.get("backface") and not config.get("double_sided") and not config.get("thin"):
            covered = False
        color = np.array(PLATE) * (BACKGROUND if config.get("thin") else np.ones(3))
        expected = color if covered else np.array(BACKGROUND)
        mirror = config.get("layer") == "reflection"
        error = float(np.max(np.abs((patch.mean(axis=(0, 1)) if mirror else patch) - expected)))
        tolerance = .003 if mirror else .001
        if error > tolerance:
            raise ValueError(f"Alpha MASK coverage mismatch in tile {tile}: {error}, budget={tolerance}")
        checks.append(dict(tile=tile, covered=covered, expected=expected.tolist(),
                           mean=patch.mean(axis=(0, 1)).tolist(), error=error, tolerance=tolerance))
    return checks


def visibility_patch(image):
    if image.ndim != 3 or image.shape[2] < 3 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid visibility image")
    h, w = image.shape[:2]
    return image[int(h * .48):int(h * .52), int(w * .48):int(w * .52), :3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    shader = (exe.parent / "shaders_PathTracing_CSMain.cso").resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    primary = [("mask", {}), ("scaled", dict(factor=.5)), ("zero-cutoff", dict(cutoff=0)),
               ("high-cutoff", dict(cutoff=1.1)), ("opaque", dict(mode="OPAQUE", factor=0)),
               ("back-one-sided", dict(backface=True)), ("back-double-sided", dict(backface=True, double_sided=True)),
               ("thin", dict(thin=True)), ("transmission-layer", dict(layer="transmission")),
               ("reflection-layer", dict(layer="reflection")), ("constant-equality", dict(texture=False, factor=.5)),
               ("nrd-fallback", {})]
    cases = [(name, "primary", config) for name, config in primary]
    for kind in ("point", "emitter"):
        cases += [(kind + "-" + variant, kind, dict(variant=variant)) for variant in ("clear", "hole", "solid")]
    cases.append(("masked-emitter", "emitter", dict(variant="clear", masked_emitter=True)))
    cases.append(("emitter-back-one-sided", "emitter", dict(variant="clear", emitter_backface=False)))
    cases.append(("emitter-back-double-sided", "emitter", dict(variant="clear", emitter_backface=True)))
    dependencies = ["alpha_mask_fixture.py", "transmission_texture_fixture.py", "validate_emissive.py"]
    report = dict(status="running", executableSha256=sha(exe), pathTracingShaderSha256=sha(shader),
                  runnerSha256=sha(Path(__file__)), expectedCases=[case[0] for case in cases], cases=[], failures=[],
                  dependencySha256={name: sha(Path(__file__).with_name(name)) for name in dependencies})
    baselines = {}
    try:
        for name, kind, config in cases:
            folder = output / name
            if kind == "primary":
                write_alpha_fixture(folder, **config)
                preset_path = folder / "render-preset.json"
            else:
                write_visibility_fixture(folder, point=kind == "point", **config)
                preset_path = folder / "preset.json"
            preset = json.loads(preset_path.read_text(encoding="utf-8"))
            frames = 64 if kind == "emitter" or config.get("layer") == "reflection" else 8
            nrd = name == "nrd-fallback"
            buffer, log = folder / "SceneColor.ptbuf", folder / "debug.log"
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
                       "-DebugPreviewResource", "PathTracing.SceneColor", "-CapturePath", str(buffer),
                       "-CaptureAfterFrames", str(frames), "-LogToFile", str(log), "-ExitAfterCapture"]
            if nrd:
                command.append("-NrdPreview")
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = child.wait(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                raise ValueError("Alpha MASK capture timeout: " + name)
            text = log.read_text(encoding="utf-8-sig")
            records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                       if line.startswith("[PathTracing] ")]
            if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(records) != 1:
                raise ValueError("Runtime or Debug Layer failure: " + name)
            state = records[0]
            meta, image = read_buffer(buffer)
            if (meta.get("resource") != "PathTracing.SceneColor" or meta.get("sampleStartIndex") != frames
                    or meta.get("samplesPerFrame") != 1 or meta.get("primarySampling") != "pixel-center"
                    or state.get("samplesPerFrame") != 1 or state.get("accumulatedSamples") != frames
                    or state.get("maxBounces") != preset["pathTracing"]["maxBounces"]
                    or state.get("pixelCenterPrimary") is not True or state.get("nrdPreviewRequested") is not nrd
                    or state.get("d3d12DebugLayerEnabledByApp") is not True):
                raise ValueError("Required alpha MASK capture state mismatch: " + name)
            if nrd and (meta.get("nrdPresentation") != FALLBACK or state.get("nrdPlannedDispatches") != 0
                        or state.get("nrdPreparedDispatches") != 0 or meta.get("nrdLastEvaluation", {}).get("recorded") is not False):
                raise ValueError("Alpha MASK NRD fallback did not bypass evaluation")
            if kind == "primary":
                if meta.get("alphaMaskActive") is not (config.get("mode", "MASK") == "MASK"):
                    raise ValueError("Alpha MASK material flag did not reach the renderer")
                assessment = assess_primary(image, config)
            else:
                patch = visibility_patch(image)
                if state.get("environmentEnabled") is not False or state.get("directLightingEnabled") is not (kind == "point"):
                    raise ValueError("Wrong visibility lighting configuration")
                if kind == "emitter" and state.get("emissiveTriangleCount") != 2:
                    raise ValueError("Expected two sampled emitter triangles")
                if config.get("masked_emitter") or config["variant"] == "solid" or config.get("emitter_backface") is False:
                    error = float(np.max(np.abs(patch)))
                    if error > 1e-7:
                        raise ValueError("Closed MASK visibility control was not black: " + name)
                elif config.get("emitter_backface") is True:
                    if kind not in baselines:
                        raise ValueError("Missing front-facing emitter baseline")
                    baseline = baselines[kind].mean(axis=(0, 1))
                    error = float(np.max(np.abs(patch.mean(axis=(0, 1)) / baseline - 1)))
                    if error > .03:
                        raise ValueError("Double-sided emitter backface differs from front baseline")
                elif config["variant"] == "clear":
                    if float(patch.mean()) <= .001:
                        raise ValueError("Visibility baseline is not illuminated")
                    baselines[kind] = patch.copy()
                    error = 0
                else:
                    if kind not in baselines:
                        raise ValueError("Missing visibility baseline")
                    error = float(np.max(np.abs(patch - baselines[kind])))
                    if error > .001:
                        raise ValueError("A MASK hole changed light visibility")
                assessment = dict(mean=patch.mean(axis=(0, 1)).tolist(), error=error)
            inputs = {str(path.relative_to(folder)): sha(path) for path in folder.rglob("*")
                      if path.is_file() and path.suffix in (".json", ".gltf", ".png")}
            report["cases"].append(dict(case=name, kind=kind, config=config, frames=frames, command=command,
                                        metadata=meta, runtime=state, assessment=assessment, inputSha256=inputs,
                                        bufferSha256=sha(buffer), logSha256=sha(log)))
            write_json(output / "report.json", report)
        if [case["case"] for case in report["cases"]] != report["expectedCases"]:
            raise ValueError("Incomplete alpha MASK case list")
        if sha(exe) != report["executableSha256"] or sha(shader) != report["pathTracingShaderSha256"]:
            raise ValueError("Runtime binaries changed during alpha MASK validation")
        report["status"] = "passed-alpha-mask"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
