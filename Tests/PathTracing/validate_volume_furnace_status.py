"""Sparse single-path status probes; not an accumulated path-frequency estimate."""
import argparse
import json
from pathlib import Path

from transmission_texture_fixture import write_json
from validate_part1 import sha
from validate_volume_furnace import assess_status
from volume_capture import capture_volume


def run(exe, source, output):
    parent = json.loads((source / "report.json").read_text(encoding="utf-8"))
    if parent.get("status") != "passed-controls-and-measured-finite-budgets":
        raise ValueError("Source furnace sweep is incomplete")
    expected = [(budget, frame) for budget in (8, 16) for frame in (1, 17)]
    output.mkdir(parents=True, exist_ok=False)
    report = dict(status="running", sourceReportSha256=sha(source / "report.json"),
                  executableSha256=sha(exe), runnerSha256=sha(Path(__file__)), cases=[])
    write_json(output / "report.json", report)
    try:
        if parent["hashes"]["executable"] != sha(exe):
            raise ValueError("Source executable differs")
        for budget, frame in expected:
            base = source / f"rough-b{budget}"
            folder = output / f"rough-b{budget}-frame{frame}"
            folder.mkdir()
            scene = json.loads((base / "scene.json").read_text(encoding="utf-8"))
            preset = json.loads((base / "render-preset.json").read_text(encoding="utf-8"))
            if scene.get("assets") or preset["pathTracing"]["debugOutput"] != 3:
                raise ValueError("Unexpected source furnace inputs")
            preset["pathTracing"]["debugOutput"] = 5
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", preset)
            evidence, image = capture_volume(exe, folder, frame,
                resource="CurrentFrameRadiance", window_size=(256, 256))
            report["cases"].append(dict(budget=budget, frame=frame,
                assessment=assess_status(image), capture=evidence))
            write_json(output / "report.json", report)
        if len(report["cases"]) != len(expected) or sha(exe) != report["executableSha256"]:
            raise ValueError("Incomplete or changed diagnostic run")
        report["status"] = "passed-sparse-status-probes"
        report["scope"] = "Two sample indices per budget; not all paths or statistical convergence"
    except Exception as error:
        report.update(status="failed", failure=str(error))
        raise
    finally:
        write_json(output / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.exe.resolve(strict=True), args.source.resolve(strict=True), args.output.resolve())
