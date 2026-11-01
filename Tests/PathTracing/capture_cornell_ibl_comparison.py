"""Capture a readable exterior Cornell comparison with matched display settings."""
import argparse
import copy
import json
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from capture_pt_hybrid_comparison import ROOT, make_pair, run
from validate_part1 import sha, write_json


def compose(output, condition, samples):
    font = ImageFont.truetype("C:/Windows/Fonts/meiryo.ttc", 25)
    small = ImageFont.truetype("C:/Windows/Fonts/meiryo.ttc", 21)
    sheet = Image.new("RGB", (2160, 842), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    title = "環境光のみ" if condition == "environment" else "環境光 ＋ 共通の平行光源"
    columns = [("hybrid-off", "従来 Hybrid（遮蔽 OFF）"),
               ("hybrid-on", "Hybrid（Diffuse IBL 遮蔽 ON）"),
               ("pt", f"Native PT（{samples} spp / 8 segments）")]
    for index, (name, label) in enumerate(columns):
        x = index * 720
        draw.text((x + 16, 10), label, font=font, fill="white")
        with Image.open(output / condition / name / "capture.png") as image:
            sheet.paste(image.convert("RGB"), (x, 58))
        draw.text((x + 16, 787), title, font=small, fill="white")
        draw.text((x + 16, 814), "同一カメラ・露出 1・ACES / RR・NRD OFF", font=small, fill=(190, 190, 190))
    sheet.save(output / (condition + "-comparison.png"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=512)
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096:
        parser.error("Samples must be in [1,4096]")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    make_pair(output, "cornell", "directional")
    template = output / "cornell" / "hybrid"
    scene = json.loads((template / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((template / "render-preset.json").read_text(encoding="utf-8"))
    scene["environment"].update(iblEnabled=True, skyColor=[.5]*3, groundColor=[.5]*3,
        backgroundIntensity=1, lightIntensity=0, fillIntensity=0, colorPanelIntensity=0)
    scene["camera"].update(position=[0, .3, -6], target=[0, -.15, .3], verticalFovDegrees=42)
    preset["lighting"].update(iblIntensity=1, diffuseIblEnabled=True, specularIblEnabled=True,
        skyboxEnabled=False, emissiveEnabled=False)
    preset["toneMap"] = dict(operatorIndex=2, exposure=1)
    preset["pathTracing"].update(environmentEnabled=True, maxBounces=8, emissiveEnabled=False)
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", scope="Visual comparison, not PT convergence or image equality certification",
        executableSha256=sha(exe), samples=args.samples, exposure=1, toneMap="ACES",
        visibilityRays=64, hybridReflections=True, records=[])
    write_json(output / "report.json", report)
    try:
        for condition in ("environment", "environment-direct"):
            for mode in ("hybrid-off", "hybrid-on", "pt"):
                folder = output / condition / mode
                folder.mkdir(parents=True)
                local, settings = copy.deepcopy(scene), copy.deepcopy(preset)
                for asset in local["assets"]:
                    asset["path"] = Path(os.path.relpath((template / asset["path"]).resolve(), folder)).as_posix()
                settings["renderingPath"] = 2 if mode == "pt" else 1
                settings["lighting"]["directLightEnabled"] = condition == "environment-direct"
                settings["pathTracing"]["directLightingEnabled"] = condition == "environment-direct"
                settings["diffuseEnvironmentVisibility"] = dict(enabled=mode == "hybrid-on", strength=1,
                    sampleCount=64, normalBias=.001, rayTMin=.0001, traceDistance=10000)
                local["description"] = "Exterior Cornell camera; matched exposure and environment. " + condition
                write_json(folder / "scene.json", local)
                write_json(folder / "render-preset.json", settings)
                result = run(exe, folder, "pt" if mode == "pt" else "hybrid", args.samples, 600, size=(720, 720))
                report["records"].append(dict(condition=condition, mode=mode, **result))
                write_json(output / "report.json", report)
                print(condition + "/" + mode, flush=True)
            compose(output, condition, args.samples)
        report["status"] = "captures-complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""))
    return report["status"] != "captures-complete"


if __name__ == "__main__":
    raise SystemExit(main())
