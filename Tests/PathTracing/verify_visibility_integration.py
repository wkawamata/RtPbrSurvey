"""Compare the built application against the approved prototype and visibility-OFF baseline."""
import argparse
import copy
import json
import os
from pathlib import Path

import numpy as np
from PIL import Image

from capture_pt_hybrid_comparison import ROOT
from study_visibility_patterns import capture
from validate_part1 import write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    base = ROOT / "bin/PathTracingValidation"
    source = base / "visibility-pattern-study-720-20261101/scrambled64-filter/lit"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", records=[])
    try:
        for name, enabled, reference in (
            ("enabled", True, source / "capture.png"),
            ("disabled", False, base / "cornell-ibl-visual-square-20261101/environment/hybrid-off/capture.png")):
            folder = output / name
            folder.mkdir()
            local, settings = copy.deepcopy(scene), copy.deepcopy(preset)
            for asset in local["assets"]:
                asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
            settings["diffuseEnvironmentVisibility"].update(enabled=enabled, sampleRotationEnabled=True,
                spatialFilterEnabled=True, filterPlaneDistance=.005, filterWorldRadius=.08)
            write_json(folder / "scene.json", local)
            write_json(folder / "render-preset.json", settings)
            record, _ = capture(exe, folder, None, 720, False, warmup=3 if enabled else 120)
            with Image.open(folder / "capture.png") as actual, Image.open(reference) as expected:
                difference = np.abs(np.asarray(actual.convert("RGBA"), dtype=np.int16) -
                                    np.asarray(expected.convert("RGBA"), dtype=np.int16))
            maximum = int(difference.max())
            # A one-code-value allowance covers optimized shader rounding; OFF must remain exact.
            passed = maximum <= (1 if enabled else 0)
            record.update(name=name, reference=str(reference), maxCodeDifference=maximum,
                          changedComponents=int(np.count_nonzero(difference)), passed=passed)
            report["records"].append(record)
            write_json(output / "report.json", report)
            print(name, "maxCodeDifference", maximum, "passed", passed, flush=True)
            if not passed:
                raise RuntimeError("Image regression: " + name)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""), flush=True)
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
