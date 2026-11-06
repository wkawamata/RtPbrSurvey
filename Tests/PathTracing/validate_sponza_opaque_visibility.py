"""Opaque-only Sponza experiment; never modify the managed source asset."""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from capture_pt_hybrid_comparison import ROOT, make_pair
from validate_part1 import sha, write_json


def opaque_variant(source, destination):
    document = json.loads(source.read_text(encoding="utf-8"))
    materials = document.get("materials", [])
    remap = {i: j for j, i in enumerate(i for i, m in enumerate(materials)
                                       if m.get("alphaMode", "OPAQUE") == "OPAQUE")}
    removed = []
    meshes = []
    mesh_remap = {}
    kept_primitives = 0
    for index, mesh in enumerate(document.get("meshes", [])):
        kept = []
        for primitive_index, primitive in enumerate(mesh["primitives"]):
            material = primitive.get("material")
            if material is not None and material not in remap:
                removed.append(dict(mesh=index, primitive=primitive_index, material=material,
                                    name=materials[material].get("name", "")))
                continue
            if material is not None:
                primitive["material"] = remap[material]
            kept.append(primitive)
        if kept:
            mesh["primitives"] = kept
            mesh_remap[index] = len(meshes)
            meshes.append(mesh)
            kept_primitives += len(kept)
    document["meshes"] = meshes
    document["materials"] = [materials[i] for i in remap]
    for node in document.get("nodes", []):
        if "mesh" in node:
            original = node.pop("mesh")
            if original in mesh_remap:
                node["mesh"] = mesh_remap[original]
    for resource in document.get("buffers", []) + document.get("images", []):
        uri = resource.get("uri")
        if uri and not uri.startswith("data:"):
            original = (source.parent / unquote(uri)).resolve()
            if not original.is_file():
                raise ValueError("Missing resource: " + str(original))
            resource["uri"] = Path(os.path.relpath(original, destination.parent)).as_posix()
    write_json(destination, document)
    return dict(sourceSha256=sha(source), variantSha256=sha(destination), removedPrimitives=removed,
                keptPrimitives=kept_primitives, keptMaterials=len(remap), removedMaterials=len(materials)-len(remap))


def gpu_metrics(text):
    groups = {}
    current_frame = 0
    for line in text.splitlines():
        total = re.match(r"\[GPU\] Frame (\d+): latest completed total ([\d.]+) ms", line)
        if total:
            current_frame = int(total[1])
            if current_frame >= 60:
                groups.setdefault("Total", []).append(float(total[2]))
        timing = re.match(r"\[GPU Pass\] (.+): ([\d.]+) ms", line)
        if timing and current_frame >= 60:
            groups.setdefault(timing[1], []).append(float(timing[2]))
    return {name: dict(samples=len(values), medianMs=float(np.median(values)),
                       p90Ms=float(np.percentile(values, 90))) for name, values in groups.items()}


def capture(exe, folder, raw=False, frames=180):
    log = folder / "debug.log"
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", "960", "540",
               "-CapturePath", str(folder / "capture.png"), "-CaptureAfterFrames", str(frames),
               "-ExitAfterCapture", "-LogToFile", str(log), "-LogFPS", "10"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    with (folder / "process.log").open("wb") as stream:
        process = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, stdout=stream, stderr=subprocess.STDOUT)
        try:
            result = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError("Capture timeout: " + str(folder))
    text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
    if result or "[ERROR]" in text or "[CORRUPTION]" in text or "effectivePath=1" not in text:
        raise RuntimeError(f"Capture failed: {folder}, exit={result}")
    with Image.open(folder / "capture.png") as image:
        if image.size != (960, 540):
            raise RuntimeError("Wrong capture dimensions")
        pixels = np.asarray(image.convert("RGB"), dtype=float) / 255
    if raw:
        magenta = (pixels[:, :, 0] > .99) & (pixels[:, :, 1] < .01) & (pixels[:, :, 2] > .99)
        if magenta.any() or np.std(pixels[:, :, 0]) < .01:
            raise RuntimeError("Unsupported or flat raw visibility; cannot evaluate this scene")
    timing = gpu_metrics(text)
    if not raw and not timing.get("Total", {}).get("samples", 0):
        raise RuntimeError("GPU timing was not logged")
    return dict(command=command, imageSha256=sha(folder / "capture.png"), logSha256=sha(log),
                warningCount=text.count("[WARNING]"), gpu=timing,
                inputSha256={p: sha(folder / p) for p in ("scene.json", "render-preset.json")})


def compose(output):
    sheet = Image.new("RGB", (1920, 600), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/meiryo.ttc", 23)
    for x, name, title in ((0, "off-a", "Diffuse IBL 遮蔽 OFF"),
                           (960, "on-a", "Diffuse IBL 遮蔽 ON：分散64本＋フィルタ")):
        with Image.open(output / name / "capture.png") as image:
            sheet.paste(image.convert("RGB"), (x, 60))
        draw.text((x+12, 8), title, fill="white", font=font)
        draw.text((x+12, 34), "MASK部分を除外／一様環境光のみ／露出1・ACES", fill="white", font=font)
    sheet.save(output / "comparison.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = ROOT / "Assets/Models/Sponza/glTF/Sponza.gltf"
    variant = output / "SponzaOpaque.gltf"
    report = dict(status="running", scope="Opaque-only modified geometry; not full Sponza or GI validation",
                  geometry=opaque_variant(source, variant), records=[])
    scene = make_pair(output, "sponza")
    preset = json.loads((output / "sponza/hybrid/render-preset.json").read_text(encoding="utf-8"))
    scene["environment"].update(iblEnabled=True, skyColor=[.5]*3, groundColor=[.5]*3,
        backgroundIntensity=1, lightIntensity=0, fillIntensity=0, colorPanelIntensity=0)
    preset["lighting"].update(lights=[], primaryShadowLightId=0, directLightEnabled=False,
        emissiveEnabled=False, skyboxEnabled=False, iblIntensity=1, diffuseIblEnabled=True, specularIblEnabled=False)
    preset["hybridReflection"].update(enabled=False, contributionEnabled=False)
    preset["toneMap"] = dict(operatorIndex=2, exposure=1)
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report.update(executableSha256=sha(exe), shaderSha256={p.name: sha(p) for p in exe.parent.glob("*.cso")})
    write_json(output / "report.json", report)
    try:
        for name in ("raw", "off-a", "on-a", "on-b", "off-b", "on-inspect"):
            folder = output / name
            folder.mkdir()
            local, settings = copy.deepcopy(scene), copy.deepcopy(preset)
            local["assets"] = [dict(id="helmet", type="gltf", path=Path(os.path.relpath(variant, folder)).as_posix())]
            settings["renderViewMode"] = 42 if name == "raw" else 0
            settings["diffuseEnvironmentVisibility"] = dict(enabled=not name.startswith("off"), strength=1,
                sampleCount=64, normalBias=.001, rayTMin=.0001, traceDistance=10000,
                sampleRotationEnabled=True, spatialFilterEnabled=True, filterPlaneDistance=.005, filterWorldRadius=.08)
            if name == "raw":
                settings["toneMap"] = dict(operatorIndex=0, exposure=1)
            elif name == "on-inspect":
                settings["toneMap"] = dict(operatorIndex=2, exposure=4)
            write_json(folder / "scene.json", local)
            write_json(folder / "render-preset.json", settings)
            record = capture(exe, folder, raw=name == "raw", frames=80 if name == "on-inspect" else 180)
            if name != "raw":
                effective = "Diffuse Environment Visibility" in record["gpu"]
                if effective != (not name.startswith("off")):
                    raise RuntimeError("Visibility GPU pass activation mismatch")
            report["records"].append(dict(name=name, **record))
            write_json(output / "report.json", report)
            print(name, "captured", record["gpu"].get("Total"), flush=True)
        if sha(source) != report["geometry"]["sourceSha256"]:
            raise RuntimeError("Managed source asset changed")
        compose(output)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""), flush=True)
    return report["status"] != "complete"


if __name__ == "__main__":
    raise SystemExit(main())
