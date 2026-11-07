"""Isolate Hybrid environment visibility; PNG statistics are display-space only."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw

from capture_pt_hybrid_comparison import ROOT, make_pair, node
from validate_part1 import sha, write_json


def prepare(output, scenes=("cornell", "sponza")):
    cases = []
    for name in scenes:
        make_pair(output, name)
        source = output / name / "hybrid"
        scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
        preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
        for asset in scene["assets"]:
            asset["path"] = str((source / asset["path"]).resolve())
        scene["environment"].update(iblEnabled=True, skyColor=[.5]*3, groundColor=[.5]*3,
            backgroundIntensity=1, lightIntensity=0, fillIntensity=0, colorPanelIntensity=0)
        preset["lighting"].update(lights=[], primaryShadowLightId=0, directLightEnabled=False,
            emissiveEnabled=False, skyboxEnabled=False, iblIntensity=1,
            diffuseIblEnabled=True, specularIblEnabled=False)
        preset["hybridReflection"].update(enabled=False, contributionEnabled=False)
        preset["pathTracing"].update(environmentEnabled=True, directLightingEnabled=False,
            emissiveEnabled=False, reconstructionBackend="native", nrdPreview=False,
            pixelCenterPrimary=True, environmentSamplingMode=7)
        if name == "cornell":
            scene["nodes"] = [scene["nodes"][0]]
            scene["camera"].update(position=[0, 0, -1.2], target=[0, 0, 1.5])
            scene["materials"].append(dict(id="closure", name="Opaque closure",
                baseColor=[.73, .73, .73, 1], metallic=0, roughness=.8))
            closure = node("closure", "primitive", materialId="closure",
                primitive=dict(kind="cube", size=1))
            closure.update(translation=[0, 0, -1.55], scale=[3.2, 3.2, .1])
            # This renderer counts primary and environment miss as two segments.
            variants = [(f"{state}-{mode}", mode, 2, state == "closed")
                for state in ("open", "closed") for mode in ("off", "diffuse", "pt")]
        else:
            variants = [("off", "off", 1, False), ("diffuse", "diffuse", 1, False),
                        ("specular", "specular", 1, False), ("both", "both", 1, False),
                        ("pt-2", "pt", 2, False), ("pt-8", "pt", 8, False)]
        for label, mode, bounces, closed in variants:
            folder = output / name / label
            folder.mkdir()
            local, settings = copy.deepcopy(scene), copy.deepcopy(preset)
            # Preserve external dependencies using paths relative to each generated scene.
            for asset in local["assets"]:
                asset["path"] = Path(os.path.relpath(asset["path"], folder)).as_posix()
            if closed:
                local["nodes"].append(closure)
            settings["renderingPath"] = 2 if mode == "pt" else 1
            settings["lighting"].update(diffuseIblEnabled=mode in ("diffuse", "both", "pt"),
                specularIblEnabled=mode in ("specular", "both", "pt"))
            settings["pathTracing"]["maxBounces"] = bounces
            write_json(folder / "scene.json", local)
            write_json(folder / "render-preset.json", settings)
            cases.append((name, label, mode, bounces, folder))
    return cases


def run(exe, case, samples, timeout):
    name, label, mode, bounces, folder = case
    image, log = folder / "capture.png", folder / "debug.log"
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", "640", "360",
        "-CapturePath", str(image), "-LogToFile", str(log), "-ExitAfterCapture"]
    command += (["-EnablePathTracing", "-PathTracingReconstruction", "native",
                 "-PathTracingSamples", str(samples)] if mode == "pt" else ["-CaptureAfterFrames", "60"])
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    with (folder / "process.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup,
            stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise RuntimeError(f"Timed out: {name}/{label}")
    text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
    if code or not image.exists() or "[ERROR]" in text or "[CORRUPTION]" in text:
        raise RuntimeError(f"Capture failed: {name}/{label}, exit={code}")
    states = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
              if line.startswith("[PathTracing] ")]
    if mode == "pt":
        if not states or states[-1]["accumulatedSamples"] != samples or states[-1]["maxBounces"] != bounces or states[-1]["reconstructionOutput"] != "native":
            raise RuntimeError("PT runtime configuration mismatch")
    elif "effectivePath=1" not in text or states:
        raise RuntimeError("Hybrid runtime path mismatch")
    with Image.open(image) as source:
        if source.size != (640, 360):
            raise RuntimeError("Unexpected resolution")
        pixels = np.asarray(source.convert("RGB"), dtype=float) / 255
    # Fixed central ROI avoids image borders and background in Cornell.
    roi = pixels[120:240, 220:420]
    luminance = roi @ np.array([.2126, .7152, .0722])
    return dict(scene=name, case=label, command=command, runtime=states,
        imageSha256=sha(image), logSha256=sha(log),
        inputSha256={p.name: sha(p) for p in (folder / "scene.json", folder / "render-preset.json")},
        roi=[220, 120, 200, 120], displayMean=float(luminance.mean()),
        displayMax=float(luminance.max()), warningCount=text.count("[WARNING]"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=128)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--scenes", nargs="+", choices=("cornell", "sponza"), default=["cornell", "sponza"])
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or args.timeout < 1 or len(args.scenes) != len(set(args.scenes)):
        parser.error("Positive samples and timeout required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", scope="Visibility isolation; no absolute PT/Hybrid equality claim",
        metric="PNG display-space; no HDR energy/convergence certification", samples=args.samples,
        commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)),
        shaderSha256={p.name: sha(p) for p in exe.parent.glob("*.cso")},
        sourceSha256={name: sha(ROOT / name) for name in
            ("Shaders/shaders_LightPass.hlsl", "Shaders/PbrLighting.hlsli",
             "Shaders/shaders_PathTracing.hlsl", "Shaders/PathTracingSampling.hlsli")}, records=[])
    write_json(output / "report.json", report)
    try:
        cases = prepare(output, args.scenes)
        for case in cases:
            report["records"].append(run(exe, case, args.samples, args.timeout))
            write_json(output / "report.json", report)
            print("Captured " + case[0] + "/" + case[1], flush=True)
        def read(name, label):
            with Image.open(output / name / label / "capture.png") as im:
                return np.asarray(im.convert("RGB"), dtype=float)[120:240, 220:420]
        if "cornell" in args.scenes:
            report["cornellChecks"] = dict(
                hybridOpenClosedMaxDifference=float(np.abs(read("cornell", "open-diffuse") - read("cornell", "closed-diffuse")).max()),
                closedPtMax=float(read("cornell", "closed-pt").max()),
                openPtMean=float(read("cornell", "open-pt").mean()),
                closedHybridMean=float(read("cornell", "closed-diffuse").mean()))
            report["controls"] = dict(
                hybridOffBlack=all(float(read("cornell", state + "-off").max()) == 0
                                   for state in ("open", "closed")),
                hybridUnaffectedByClosure=report["cornellChecks"]["hybridOpenClosedMaxDifference"] == 0,
                ptClosedBlack=report["cornellChecks"]["closedPtMax"] == 0,
                ptOpenNonblack=report["cornellChecks"]["openPtMean"] > 1,
                hybridClosedNonblack=report["cornellChecks"]["closedHybridMean"] > 1)
        for name in args.scenes:
            selected = [c for c in cases if c[0] == name]
            sheet = Image.new("RGB", (640*3, 394*2), (24, 24, 24))
            draw = ImageDraw.Draw(sheet)
            for index, case in enumerate(selected):
                x, y = index % 3 * 640, index // 3 * 394
                with Image.open(case[4] / "capture.png") as im:
                    sheet.paste(im.convert("RGB"), (x, y+34))
                draw.text((x+10, y+10), name + " / " + case[1], fill="white")
            sheet.save(output / (name + "-contact.png"))
        report["status"] = ("captures-complete" if all(report.get("controls", {}).values())
                            else "diagnostic-controls-failed")
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("status", "error", "cornellChecks") if k in report}))
    return report["status"] != "captures-complete"


if __name__ == "__main__":
    raise SystemExit(main())
