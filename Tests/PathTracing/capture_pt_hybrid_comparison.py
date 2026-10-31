"""Capture matched-camera PT/Hybrid illustrations, not a correctness acceptance test."""
import argparse
import base64
import copy
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / "Assets/Scene/PathTracingValidation/32-helmet-reflection"
NAMES = ("cornell", "sponza", "helmet")


def cornell_asset(path):
    raw, views, accessors, primitives = bytearray(), [], [], []
    faces = [([-1.5, 0, 0], [0, 0, 1.5], [0, 1.5, 0], [1, 0, 0], 0),
             ([1.5, 0, 0], [0, 0, 1.5], [0, 1.5, 0], [-1, 0, 0], 1),
             ([0, 0, 1.5], [1.5, 0, 0], [0, 1.5, 0], [0, 0, -1], 2),
             ([0, -1.5, 0], [1.5, 0, 0], [0, 0, 1.5], [0, 1, 0], 2),
             ([0, 1.5, 0], [1.5, 0, 0], [0, 0, 1.5], [0, -1, 0], 2)]
    for center, u, v, normal, material in faces:
        c, u, v, n = map(lambda x: np.asarray(x, dtype=float), (center, u, v, normal))
        positions = np.array([c-u-v, c+u-v, c+u+v, c-u+v])
        indices = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.uint16)
        if np.dot(np.cross(u, v), n) < 0:
            indices = indices[:, [0, 2, 1]]
        positions[:, 2] *= -1
        n[2] *= -1
        indices = indices[:, [0, 2, 1]]
        start = len(accessors)
        for data, kind, component, count in (
                (positions.astype("<f4").tobytes(), "VEC3", 5126, 4),
                (np.tile(n, (4, 1)).astype("<f4").tobytes(), "VEC3", 5126, 4),
                (indices.astype("<u2").tobytes(), "SCALAR", 5123, 6)):
            while len(raw) % 4:
                raw.append(0)
            views.append(dict(buffer=0, byteOffset=len(raw), byteLength=len(data)))
            raw.extend(data)
            accessors.append(dict(bufferView=len(views)-1, componentType=component, count=count, type=kind))
        accessors[start].update(min=positions.min(axis=0).tolist(), max=positions.max(axis=0).tolist())
        primitives.append(dict(attributes=dict(POSITION=start, NORMAL=start+1), indices=start+2, material=material))
    materials = [dict(name=name, pbrMetallicRoughness=dict(baseColorFactor=[*color, 1],
                  metallicFactor=0, roughnessFactor=.8)) for name, color in
                 (("Red wall", [.65, .03, .03]), ("Green wall", [.03, .55, .03]), ("White", [.73]*3))]
    write_json(path, dict(asset=dict(version="2.0"),
        buffers=[dict(byteLength=len(raw), uri="data:application/octet-stream;base64," + base64.b64encode(raw).decode())],
        bufferViews=views, accessors=accessors, materials=materials,
        meshes=[dict(primitives=primitives)], nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0))


def node(name, kind, **kwargs):
    return dict(id=name, name=name, type=kind, parentId=None, translation=[0, 0, 0],
                rotation=[0, 0, 0, 1], scale=[1, 1, 1], visible=True, **kwargs)


def make_pair(output, name, cornell_light="directional"):
    scene = json.loads((TEMPLATE / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((TEMPLATE / "render-preset.json").read_text(encoding="utf-8"))
    shared = output / name
    shared.mkdir()
    scene.update(name="PT Hybrid " + name, sceneId="pt-hybrid-" + name,
                 description="Matched camera/environment/exposure illustration; no image-equality requirement.")
    scene["assets"][0]["path"] = Path(os.path.relpath(
        ROOT / "Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf", shared)).as_posix()
    preset["temporalUpscaler"] = dict(enabled=False, renderScale=1)
    preset["rayReconstruction"] = dict(enabled=False, experimentalNativeEvaluationEnabled=False)
    preset["hybridReflection"] = dict(enabled=True, contributionEnabled=True, contributionIntensity=1,
        materialGateEnabled=True, maxRoughness=.8, minMetallic=0, contributionMaxDistance=100,
        hitOverlayEnabled=False, temporalHistoryWeight=.9, temporalNoiseStrength=0)
    preset["backBufferClearColor"] = [0, 0, 0, 1]
    preset["lighting"].update(skyboxPreview=False, skyboxPreviewExposure=1, iblIntensity=1)
    preset["pathTracing"].update(maxBounces=8, pixelCenterPrimary=False,
        denoiserDistanceDiagnostics=False, nrdPreview=False, reconstructionBackend="native",
        reconstructionDiagnostics=False, emissiveSamplingMode=2, russianRouletteEnabled=False)
    preset["shadow"].update(enabled=True, softShadowEnabled=False, normalBias=.0001, rayTMin=.0001)
    if name == "sponza":
        scene["assets"][0]["path"] = Path(os.path.relpath(
            ROOT / "Assets/Models/Sponza/glTF/Sponza.gltf", shared)).as_posix()
        scene["nodes"][0].update(id="sponza", name="Sponza", assetId="helmet")
        scene["camera"].update(position=[-9, 1.8, 0], target=[5, 3, 0], verticalFovDegrees=65)
        scene["environment"].update(skyColor=[.7, .8, 1], groundColor=[.08, .08, .08],
            lightDirection=[0, 1, 0], lightColor=[1, .95, .85], lightIntensity=3, lightSize=.15)
    elif name == "cornell":
        cornell_asset(shared / "cornell.gltf")
        scene["assets"] = [dict(id="room", type="gltf", path="cornell.gltf")]
        scene["materials"] = [dict(id="mirror", name="Mirror", baseColor=[.9, .9, .9, 1], metallic=1, roughness=.02),
                               dict(id="diffuse", name="Diffuse", baseColor=[.73, .73, .73, 1], metallic=0, roughness=.8)]
        mirror = node("Mirror sphere", "primitive", materialId="mirror",
                      primitive=dict(kind="sphere", radius=.55, slices=96, stacks=48))
        mirror["translation"] = [-.55, -.95, .25]
        diffuse = node("Diffuse sphere", "primitive", materialId="diffuse",
                       primitive=dict(kind="sphere", radius=.4, slices=96, stacks=48))
        diffuse["translation"] = [.65, -1.1, -.15]
        scene["nodes"] = [node("Room", "gltf", assetId="room"), mirror, diffuse]
        scene["camera"].update(position=[0, 0, -5], target=[0, -.1, .1], verticalFovDegrees=42)
        scene["environment"]["iblEnabled"] = False
        preset["lighting"].update(iblIntensity=0, skyboxEnabled=False, directLightEnabled=True,
            diffuseIblEnabled=False, specularIblEnabled=False, emissiveEnabled=False, primaryShadowLightId=1,
            lights=[dict(id=1, name="Shared point", type="point", enabled=True, color=[1, 1, 1],
                         intensity=8, position=[0, 1.25, -.4], direction=[0, -1, 0], range=20,
                         innerCone=15, outerCone=30)])
        preset["pathTracing"].update(environmentEnabled=False, directLightingEnabled=True, emissiveEnabled=False)
        if cornell_light == "directional":
            preset["lighting"]["lights"][0].update(name="Shared directional", type="directional",
                intensity=2, direction=[.25, -.6, 1])
            scene["description"] += " Shared directional light enters through the open front; ceiling remains opaque."
    for mode in ("hybrid", "pt"):
        variant = shared / mode
        variant.mkdir()
        local = copy.deepcopy(scene)
        for asset in local["assets"]:
            asset["path"] = Path(os.path.relpath((shared / asset["path"]).resolve(), variant)).as_posix()
        settings = copy.deepcopy(preset)
        settings["renderingPath"] = 2 if mode == "pt" else 1
        write_json(variant / "scene.json", local)
        write_json(variant / "render-preset.json", settings)
    return scene


def run(exe, folder, mode, samples, timeout):
    image, log = folder / "capture.png", folder / "debug.log"
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", "960", "540",
               "-CapturePath", str(image), "-LogToFile", str(log), "-ExitAfterCapture"]
    command += (["-EnablePathTracing", "-PathTracingReconstruction", "native", "-PathTracingSamples", str(samples)]
                if mode == "pt" else ["-CaptureAfterFrames", "120"])
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    with (folder / "process.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise ValueError("Capture timed out: " + str(folder))
    text = log.read_text(encoding="utf-8-sig")
    if code or "[ERROR]" in text or "[CORRUPTION]" in text or not image.is_file():
        raise ValueError("Capture/Debug Layer failure: " + str(folder))
    states = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines() if line.startswith("[PathTracing] ")]
    if mode == "pt" and (len(states) != 1 or states[0].get("accumulatedSamples") != samples or
                         states[0].get("reconstructionOutput") != "native" or
                         states[0].get("maxBounces") != 8 or states[0].get("nrdPreviewRequested") is not False):
        raise ValueError("PT sample/backend mismatch")
    if mode == "hybrid" and ("effectivePath=1" not in text or states):
        raise ValueError("Hybrid rendering-path mismatch")
    with Image.open(image) as png:
        if png.size != (960, 540):
            raise ValueError("Wrong capture dimensions")
    return dict(command=command, runtime=states, imageSha256=sha(image), logSha256=sha(log),
                inputSha256={p.name: sha(p) for p in (folder / "scene.json", folder / "render-preset.json")})


def comparison(output, name, samples, cornell_light="directional"):
    canvas = Image.new("RGB", (1920, 588), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 22)
    for x, mode, title in ((0, "hybrid", "Hybrid (Deferred + ray-traced reflections)"),
                            (960, "pt", f"Native PT - {samples} spp / 8 bounces")):
        with Image.open(output / name / mode / "capture.png") as source:
            canvas.paste(source.convert("RGB"), (x, 48))
        label = name.title() + (" (directional)" if name == "cornell" and cornell_light == "directional" else "")
        draw.text((x+15, 12), label + " | " + title, font=font, fill="white")
    canvas.save(output / (name + "-comparison.png"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--scenes", nargs="+", choices=NAMES, default=list(NAMES))
    parser.add_argument("--cornell-light", choices=("point", "directional"), default="directional",
                        help="Directional supports matched shadows; point has no Hybrid shadow in the current renderer")
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or args.timeout < 1 or len(args.scenes) != len(set(args.scenes)):
        parser.error("Invalid samples/timeout")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", scope="Visual comparison, not image-equality or performance acceptance",
                  samples=args.samples, scenes=args.scenes, cornellLight=args.cornell_light,
                  executableSha256=sha(exe), records=[])
    write_json(output / "report.json", report)
    try:
        for name in args.scenes:
            make_pair(output, name, args.cornell_light)
            for mode in ("hybrid", "pt"):
                record = run(exe, output / name / mode, mode, args.samples, args.timeout)
                report["records"].append(dict(scene=name, mode=mode, **record))
                write_json(output / "report.json", report)
                print("Captured " + name + " " + mode, flush=True)
            comparison(output, name, args.samples, args.cornell_light)
        report["status"] = "captures-complete"
    except Exception as error:
        report.update(status="failed", failure=str(error))
    write_json(output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("status", "failure") if k in report}))
    return report["status"] == "failed"


if __name__ == "__main__":
    raise SystemExit(main())
