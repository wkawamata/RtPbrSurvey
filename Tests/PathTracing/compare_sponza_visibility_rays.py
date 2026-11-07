"""Compare ray-count cost and presentation, not physical correctness."""
import argparse
import copy
import json
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from validate_sponza_opaque_visibility import ROOT, capture
from validate_part1 import sha, write_json


def compose(output):
    sheet = Image.new("RGB", (960, 1710), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/meiryo.ttc", 23)
    for index, count in enumerate((64, 32, 16)):
        y = index * 570
        settings = json.loads((output / str(count) / "render-preset.json").read_text(encoding="utf-8"))
        exposure = settings["toneMap"]["exposure"]
        draw.text((12, y+2), f"{count} rays/pixel / 同一露出{exposure:g}・フィルタON", font=font, fill="white")
        with Image.open(output / str(count) / "capture.png") as image:
            sheet.paste(image.convert("RGB"), (0, y+30))
    sheet.save(output / "comparison.png")
    regions = {"floor": (340, 370, 620, 530), "column": (140, 125, 340, 420),
               "back-wall": (360, 200, 590, 340)}
    for label, rect in regions.items():
        width, height = rect[2]-rect[0], rect[3]-rect[1]
        detail = Image.new("RGB", (width*2, (height*2+34)*3), (24, 24, 24))
        draw = ImageDraw.Draw(detail)
        for index, count in enumerate((64, 32, 16)):
            y = index*(height*2+34)
            draw.text((8, y+2), f"{count} rays/pixel / 2x nearest", font=font, fill="white")
            with Image.open(output / str(count) / "capture.png") as image:
                detail.paste(image.convert("RGB").crop(rect).resize((width*2, height*2), Image.Resampling.NEAREST), (0,y+34))
        detail.save(output / (label+"-detail.png"))


def capture_case(source, output, count, exposure, frames=80):
    folder = output / str(count)
    folder.mkdir(parents=True, exist_ok=False)
    local = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    settings = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
    for asset in local["assets"]:
        asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
    settings["diffuseEnvironmentVisibility"]["sampleCount"] = count
    settings["toneMap"]["exposure"] = exposure
    write_json(folder / "scene.json", local)
    write_json(folder / "render-preset.json", settings)
    record = capture(ROOT / "bin/x64/Debug/RtPbrSurvey.exe", folder, frames=frames)
    write_json(folder / "capture-record.json", record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", executableSha256=sha(exe), records=[],
                  scope="Debug, opaque-only Sponza, static camera, exposure 4, no temporal history")
    images = []
    try:
        for count in (64, 32, 16):
            folder = output / str(count)
            folder.mkdir()
            local, settings = copy.deepcopy(scene), copy.deepcopy(preset)
            for asset in local["assets"]:
                asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
            settings["diffuseEnvironmentVisibility"]["sampleCount"] = count
            write_json(folder / "scene.json", local)
            write_json(folder / "render-preset.json", settings)
            record = capture(exe, folder, frames=120)
            if "Diffuse Environment Visibility" not in record["gpu"]:
                raise RuntimeError("Visibility pass is not active")
            with Image.open(folder / "capture.png") as image:
                images.append(image.convert("RGB").copy())
            difference = np.asarray(images[-1], dtype=float) - np.asarray(images[0], dtype=float)
            record.update(rays=count, displayRmseVs64=float(np.sqrt(np.mean(difference**2))),
                          displayMaxCodeDifference=float(np.abs(difference).max()))
            report["records"].append(record)
            write_json(output / "report.json", report)
            print(count, record["gpu"]["Total"], flush=True)
        compose(output)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""), flush=True)
    return report["status"] != "complete"


if __name__ == "__main__":
    raise SystemExit(main())
