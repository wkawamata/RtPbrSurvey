"""Run editable Part 1 fixtures; retain failed runs and ignored capture artifacts."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

from compare_hdr import capture, rmse


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(value, indent=2, ensure_ascii=False) + "\n").replace("\n", "\r\n").encode("utf-8"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def falloff(distance, light_range):
    return max(0, 1 - (distance / light_range) ** 4) ** 2 / distance ** 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--output", type=Path, default=root / "bin/PathTracingValidation/part1")
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.samples < 1 or args.timeout < 1:
        parser.error("Samples and timeout must be positive")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    source = root / "Assets/Scenes/PathTracingValidation"
    plan = json.loads((source / "validation-plan.json").read_text())
    records, images, failures = {}, {}, []
    report = dict(schemaVersion=1, baseCommit="816c3f7", testedCommit=subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(), workspace=str(root),
        branch=subprocess.check_output(["git", "branch", "--show-current"], cwd=root, text=True).strip(),
        build="Debug x64", samples=args.samples, seed=plan["seed"], roi=plan["roi"],
        thresholds=plan, captures=records, failures=failures, checks={})
    report["generatedUtc"] = datetime.now(timezone.utc).isoformat()
    report["dirty"] = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip())
    try:
        report["gpuDriver"] = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"],
            text=True, timeout=10).strip()
    except (OSError, subprocess.SubprocessError) as error:
        report["gpuDriverQueryError"] = str(error)
    write_json(args.output / "report.json", report)

    def run(name, scene_id, change=None, mode=0, seed=None, png=False):
        scene_path = source / scene_id / "scene.json"
        preset_path = source / scene_id / "render-preset.json"
        scene = json.loads(scene_path.read_text())
        preset = json.loads(preset_path.read_text())
        if preset["pathTracing"].get("debugOutput") != 3:
            raise ValueError("Validation requires Radiance output (3)")
        if change:
            change(scene, preset)
        scene["renderPreset"] = name + "-preset.json"
        derived_scene = args.output / (name + "-scene.json")
        derived_preset = args.output / scene["renderPreset"]
        write_json(derived_scene, scene)
        write_json(derived_preset, preset)
        request = SimpleNamespace(root=root, exe=root / "bin/x64/Debug/RtPbrSurvey.exe",
            scene_file=derived_scene, render_preset=derived_preset, output=args.output,
            timeout=args.timeout, roi=plan["roi"])
        command = [str(request.exe), "-SceneFile", str(derived_scene), "-RenderPreset", str(derived_preset),
            "-EnablePathTracing", "-PathTracingSamples", str(args.samples), "-PathTracingSeed", str(seed or plan["seed"]),
            "-PathTracingEnvironmentMode", str(mode), "-CapturePath", str(args.output / (name + ".pfm")),
            "-LogToFile", str(args.output / (name + ".log")), "-ExitAfterCapture"]
        try:
            record, pixels = capture(request, mode, seed or plan["seed"], args.samples, name)
            if list(record["dimensions"]) != plan["resolution"]:
                raise RuntimeError("Resolution differs from validation plan")
            diagnostics = record["diagnostics"]
            if diagnostics["maxBounces"] != preset["pathTracing"]["maxBounces"]:
                raise RuntimeError("Bounce count differs from preset")
            record.update(command=command, sceneHash=sha(derived_scene), presetHash=sha(derived_preset),
                roi=plan["roi"],
                sourceSceneHash=sha(scene_path), sourcePresetHash=sha(preset_path),
                meanRgb=[sum(pixels[c::3]) / (len(pixels) // 3) for c in range(3)])
            records[name], images[name] = record, pixels
            if png:
                png_command = command.copy()
                png_command[png_command.index("-CapturePath") + 1] = str(args.output / (name + ".png"))
                png_command[png_command.index("-LogToFile") + 1] = str(args.output / (name + "-png.log"))
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                subprocess.run(png_command, cwd=root, check=True, timeout=args.timeout, startupinfo=startup)
                png_path = args.output / (name + ".png")
                png_log = args.output / (name + "-png.log")
                if not png_path.is_file() or any(line.startswith(("[ERROR]", "[CORRUPTION]"))
                        for line in png_log.read_text(encoding="utf-8-sig").splitlines()):
                    raise RuntimeError("PNG missing or D3D12 error")
                record.update(pngPath=str(png_path), pngHash=sha(png_path), pngCommand=png_command)
        except Exception as error:
            failures.append(dict(name=name, command=command, error=str(error)))
        write_json(args.output / "report.json", report)

    for scene_id in plan["scenes"]:
        run(scene_id, scene_id, mode=5 if scene_id in ("constant-environment", "roughness") else 0, png=True)
    run("constant-repeat", "constant-environment", mode=5)
    run("constant-seed8", "constant-environment", mode=5, seed=8)
    run("constant-bsdf", "constant-environment", mode=1)
    run("constant-nee", "constant-environment", mode=2)

    def clear(scene, preset):
        scene["nodes"] = [n for n in scene["nodes"] if n["id"] != "blocker"]

    def beyond(scene, preset):
        next(n for n in scene["nodes"] if n["id"] == "blocker")["translation"] = [-3, 4.5, 0]

    run("visibility-clear", "single-light-visibility", clear)
    run("visibility-beyond", "single-light-visibility", beyond)
    for height in plan["falloff"]["heights"]:
        def height_variant(scene, preset, height=height):
            clear(scene, preset)
            preset["lighting"]["lights"][0]["position"] = [0, height, 0]
            preset["shadow"]["enabled"] = False
        run(f"falloff-{height}", "single-light-visibility", height_variant)
    def direct(scene, preset):
        preset["pathTracing"]["maxBounces"] = 1
    run("indirect-one-bounce", "two-surface-indirect", direct)
    if not failures:
        checks = report["checks"]
        checks["fixedSeedRepeat"] = records["constant-environment"]["sha256"] == records["constant-repeat"]["sha256"]
        checks["seedVariation"] = rmse(images["constant-environment"], images["constant-seed8"]) > 0
        a, b, c = [sum(images[n]) / len(images[n]) for n in
            ("visibility-clear", "single-light-visibility", "visibility-beyond")]
        difference = max(abs(x-y) for x, y in zip(images["visibility-clear"], images["visibility-beyond"]))
        report["visibility"] = dict(clearMean=a, betweenMean=b, beyondMean=c, maxBeyondDifference=difference)
        limits = plan["visibility"]
        checks["visibility"] = a > limits["minClearMean"] and b < a * limits["maxBetweenRatio"] and difference <= limits["maxBeyondDifference"]
        measured = sum(images["falloff-2"]) / sum(images["falloff-4"])
        expected = falloff(2, 20) / falloff(4, 20)
        relative_error = abs(measured / expected - 1)
        report["falloff"] = dict(measuredRatio=measured, expectedRatio=expected, relativeError=relative_error)
        checks["falloff"] = relative_error <= plan["falloff"]["relativeTolerance"]
        report["indirect"] = dict(oneBounceMeanRgb=records["indirect-one-bounce"]["meanRgb"],
            fourBounceMeanRgb=records["two-surface-indirect"]["meanRgb"])
    report["status"] = "passed" if not failures and all(report["checks"].values()) else "failed"
    write_json(args.output / "report.json", report)
    print(json.dumps(dict(status=report["status"], checks=report["checks"], failures=failures)))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
