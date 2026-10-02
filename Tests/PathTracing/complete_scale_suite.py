"""Part 3 supplementary TMax/near-light runs and editable fixture generation."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

from compare_hdr import capture, build_capture_command
from validate_part1 import write_json
from validate_scale import ROOT, SCALES, make_case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--powershell", required=True)
    parser.add_argument("--write-fixtures", action="store_true")
    args = parser.parse_args()
    if args.write_fixtures:
        for scale, label in zip(SCALES, ("small", "medium", "large")):
            scene, preset = make_case(scale, variant="contact")
            directory = ROOT / "Assets/Scenes/PathTracingValidation" / ("scale-" + label)
            scene["renderPreset"] = "render-preset.json"
            write_json(directory / "scene.json", scene)
            write_json(directory / "render-preset.json", preset)
        return
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Use an empty output directory")
    output.mkdir(parents=True, exist_ok=True)
    report = dict(tmax=[], nearLight=[], failures=[])
    for scale in SCALES:
        for distance in (2, 20):
            scene, preset = make_case(scale)
            preset["shadow"]["rayTMax"] = distance * scale
            name = f"tmax-{scale}-{distance}"
            scene_path = output / (name + "-scene.json")
            preset_path = output / (name + "-preset.json")
            scene["renderPreset"] = preset_path.name
            write_json(scene_path, scene)
            write_json(preset_path, preset)
            request = SimpleNamespace(root=ROOT, exe=ROOT / "bin/x64/Debug/RtPbrSurvey.exe",
                scene_file=scene_path, render_preset=preset_path, output=output,
                timeout=180, roi=[944, 524, 32, 32])
            command = build_capture_command(request, 0, 7, 4, output / (name + ".pfm"), output / (name + ".log"))
            try:
                record, pixels = capture(request, 0, 7, 4, name)
                record.update(scale=scale, normalizedTMax=distance, mean=sum(pixels)/len(pixels), command=command)
                report["tmax"].append(record)
            except Exception as error:
                report["failures"].append(dict(command=command, error=str(error)))
                raise
            finally:
                write_json(output / "report.json", report)
    for kind in ("Point", "Spot"):
        for offset in (0, 1):
            directory = output / f"near-{kind}-{offset}"
            command = [args.powershell, "-NoProfile", "-File", str(ROOT / "Tests/PathTracing/Test-LocalLightVisibility.ps1"),
                "-OutputDirectory", str(directory), "-LightType", kind, "-NearLightBoundary",
                "-LightOffsetX", str(offset), "-Samples", "4", "-Seed", "7"]
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=600)
            (output / f"near-{kind}-{offset}.txt").write_bytes((result.stdout + result.stderr).encode("utf-8"))
            record = dict(command=command, exitCode=result.returncode, report=str(directory / "report.json"))
            if (directory / "report.json").exists():
                record["metrics"] = json.loads((directory / "report.json").read_text(encoding="utf-8-sig"))
            report["nearLight"].append(record)
            if result.returncode:
                report["failures"].append(record)
            write_json(output / "report.json", report)
    report["artifacts"] = [dict(path=str(p.relative_to(ROOT)), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        for p in output.rglob("*") if p.is_file() and p.name != "report.json"]
    write_json(output / "report.json", report)


if __name__ == "__main__":
    main()
