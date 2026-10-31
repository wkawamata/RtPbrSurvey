"""Measure the existing Native PT roughness plateau without changing the renderer."""
import argparse
import base64
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import subprocess

import numpy as np
from PIL import Image

from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / "Assets/Scene/PathTracingValidation/32-helmet-reflection"
CASES = (("zero", 0.0), ("helmet-median", .00746155), ("below-floor", .03),
         ("above-floor", .06), ("rough-control", .15))
SOURCES = ("Shaders/PathTracingSampling.hlsli", "Shaders/PathTracingDenoiserSampling.hlsli",
           "Shaders/shaders_PathTracing.hlsl", "Tests/PathTracing/validate_near_mirror.py",
           "Tests/PathTracing/test_near_mirror.py")


def reflection_quantile(roughness, probability, floor=0.0):
    """Normal-incidence GGX NDF: P(tan(theta_h)<=t)=t^2/(alpha^2+t^2)."""
    if not math.isfinite(roughness) or not 0 <= roughness <= 1:
        raise ValueError("Roughness must be finite and in [0,1]")
    if not 0 < probability < 1 or not math.isfinite(floor) or floor < 0:
        raise ValueError("Invalid quantile or alpha floor")
    alpha = max(roughness * roughness, floor)
    return math.degrees(2 * math.atan(alpha * math.sqrt(probability / (1 - probability))))


def fixture(folder, roughness):
    folder.mkdir(parents=True, exist_ok=False)
    scene = json.loads((TEMPLATE / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((TEMPLATE / "render-preset.json").read_text(encoding="utf-8"))
    raw = bytearray()
    views, accessors, meshes = [], [], []
    # glTF +Z becomes renderer -Z. The emitter is behind the camera, so it
    # cannot appear on primary rays; the mirror is its only visible path.
    for width, height, z, normal, indices, material in (
            (6, 4, 0, (0, 0, 1), (0, 1, 2, 0, 2, 3), 0),
            (.04, 6, 6, (0, 0, -1), (0, 2, 1, 0, 3, 2), 1)):
        positions = [(-width/2, -height/2, z), (width/2, -height/2, z),
                     (width/2, height/2, z), (-width/2, height/2, z)]
        first = len(accessors)
        for payload, kind, count, component in (
                (struct.pack("<12f", *(v for p in positions for v in p)), "VEC3", 4, 5126),
                (struct.pack("<12f", *(normal * 4)), "VEC3", 4, 5126),
                (struct.pack("<6H", *indices), "SCALAR", 6, 5123)):
            while len(raw) % 4:
                raw.append(0)
            views.append(dict(buffer=0, byteOffset=len(raw), byteLength=len(payload)))
            raw.extend(payload)
            accessors.append(dict(bufferView=len(views)-1, componentType=component, count=count, type=kind))
        accessors[first].update(min=[-width/2, -height/2, z], max=[width/2, height/2, z])
        meshes.append(dict(primitives=[dict(attributes=dict(POSITION=first, NORMAL=first+1),
                                           indices=first+2, material=material)]))
    gltf = dict(asset=dict(version="2.0"),
                buffers=[dict(byteLength=len(raw), uri="data:application/octet-stream;base64," +
                              base64.b64encode(raw).decode("ascii"))],
                bufferViews=views, accessors=accessors, meshes=meshes,
                materials=[dict(name="Flat metal", pbrMetallicRoughness=dict(
                    baseColorFactor=[.8, .8, .8, 1], metallicFactor=1, roughnessFactor=roughness)),
                    dict(name="Reflection stripe", pbrMetallicRoughness=dict(
                        baseColorFactor=[0, 0, 0, 1], metallicFactor=0, roughnessFactor=1),
                         emissiveFactor=[1, 1, 1])],
                nodes=[dict(mesh=0), dict(mesh=1)], scenes=[dict(nodes=[0, 1])], scene=0)
    write_json(folder / "target.gltf", gltf)
    scene.update(sceneId="near-mirror-reflection", name="PT near-mirror reflection",
                 description="Native PT roughness-floor diagnostic; reflected emissive stripe, no IBL.",
                 assets=[dict(id="target", type="gltf", path="target.gltf")], materials=[],
                 nodes=[dict(id="target", name="Mirror and stripe", type="gltf", assetId="target",
                             parentId=None, translation=[0, 0, 0], rotation=[0, 0, 0, 1],
                             scale=[1, 1, 1], visible=True)])
    scene["camera"].update(position=[0, 0, -5], target=[0, 0, 0], up=[0, 1, 0], verticalFovDegrees=30)
    scene["environment"]["iblEnabled"] = False
    preset["backBufferClearColor"] = [0, 0, 0, 1]
    preset["lighting"].update(lights=[], iblIntensity=0, skyboxEnabled=False,
                               directLightEnabled=False, emissiveEnabled=True)
    preset["pathTracing"].update(accumulate=True, samplesPerFrame=1, maxBounces=2, randomSeed=7,
                                  directLightingEnabled=False, environmentEnabled=False,
                                  emissiveEnabled=True, emissiveSamplingMode=0, debugOutput=3,
                                  pixelCenterPrimary=True, denoiserDistanceDiagnostics=False,
                                  nrdPreview=False, russianRouletteEnabled=False)
    write_json(folder / "scene.json", scene)
    write_json(folder / "render-preset.json", preset)


def assess(images):
    if set(images) != {name for name, _ in CASES}:
        raise ValueError("Missing or unexpected roughness case")
    metrics = {}
    for name, image in images.items():
        if image.shape != (360, 640, 4) or not np.isfinite(image).all():
            raise ValueError("Invalid capture: " + name)
        rgb = image[:, :, :3]
        if rgb.min() < 0 or rgb.max() > 1.01:
            raise ValueError("Nonphysical radiance in unit-emission scene: " + name)
        profile = rgb[120:240].mean(axis=(0, 2))
        energy = float(profile.sum())
        if energy <= .01 or float(profile.max()) < .01:
            raise ValueError("Reflected target missing: " + name)
        position = np.arange(640) - 319.5
        metrics[name] = dict(peak=float(profile.max()), rowSum=energy,
                             rmsWidthPixels=float(np.sqrt(np.sum(profile * position**2) / energy)))
    baseline = images["zero"]
    for name in ("helmet-median", "below-floor"):
        if not np.array_equal(baseline, images[name]):
            raise ValueError("Expected current-floor plateau was not reproduced: " + name)
    for name in ("above-floor", "rough-control"):
        delta = float(np.max(np.abs(baseline[:, :, :3] - images[name][:, :, :3])))
        if delta <= .001:
            raise ValueError("Roughness control did not change the image: " + name)
        metrics[name]["maxDifferenceFromZero"] = delta
    return dict(plateauExactlyEqual=True, metrics=metrics)


def capture(exe, folder, samples, timeout):
    path, log = folder / "capture.ptbuf", folder / "debug.log"
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
               "-PathTracingReconstruction", "native", "-WindowSize", "640", "360",
               "-PathTracingSamples", str(samples), "-DebugPreviewResource", "PathTracing.SceneColor",
               "-CapturePath", str(path), "-LogToFile", str(log), "-ExitAfterCapture"]
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
            raise ValueError("Capture timed out: " + folder.name)
    text = log.read_text(encoding="utf-8-sig")
    states = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
              if line.startswith("[PathTracing] ")]
    if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(states) != 1:
        raise ValueError("Runtime/Debug Layer failure: " + folder.name)
    expected = dict(scene="PT near-mirror reflection", reconstructionRequested="native",
                    reconstructionOutput="native", accumulatedSamples=samples, randomSeed=7,
                    maxBounces=2, samplesPerFrame=1, pixelCenterPrimary=True, environmentEnabled=False,
                    directLightingEnabled=False, emissiveEnabled=True, emissiveSamplingMode=0,
                    nrdPreviewRequested=False, russianRoulette=False,
                    denoiserDistanceDiagnostics=False, renderWidth=640, renderHeight=360,
                    d3d12DebugLayerEnabledByApp=True)
    if any(type(states[0].get(k)) is not type(v) or states[0][k] != v for k, v in expected.items()):
        raise ValueError("Runtime settings mismatch: " + folder.name)
    meta, image = read_buffer(path)
    if (meta.get("resource") != "PathTracing.SceneColor" or meta.get("width") != 640 or
            meta.get("height") != 360 or meta.get("primarySampling") != "pixel-center" or
            meta.get("sampleStartIndex") != samples or meta.get("randomSeed") != 7 or
            meta.get("reconstructionBackend") != "native" or meta.get("nrdPreviewRequested") is not False):
        raise ValueError("Capture metadata mismatch")
    return dict(command=command, runtime=states[0], metadata=meta,
                captureSha256=sha(path), logSha256=sha(log)), image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/x64/Debug/RtPbrSurvey.exe")
    parser.add_argument("--samples", type=int, default=256)
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()
    if not 64 <= args.samples <= 4096 or args.timeout <= 0:
        parser.error("Require 64..4096 samples and positive timeout")
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = dict(schemaVersion=1, status="running", generatedUtc=datetime.now(timezone.utc).isoformat(),
                  testedCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                  workingTreeModified=bool(subprocess.check_output(
                      ["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
                  executableSha256=sha(exe), sourceSha256={p: sha(ROOT / p) for p in SOURCES},
                  expectedCases=[name for name, _ in CASES], samples=args.samples, records=[],
                  scope="Current-floor reproduction, not a physical accuracy or convergence acceptance test",
                  qualityAccepted=False, angularReference=[dict(name=name, roughness=r,
                      unclampedMedianDegrees=reflection_quantile(r, .5),
                      currentMedianDegrees=reflection_quantile(r, .5, .001),
                      unclamped90PercentDegrees=reflection_quantile(r, .9),
                      current90PercentDegrees=reflection_quantile(r, .9, .001)) for name, r in CASES])
    report["angularReferenceScope"] = ("Analytic unconditioned NDF-sampled reflection at normal incidence; "
        "not a radiance-profile prediction, pixel footprint, Fresnel/Smith weighting or GPU accuracy proof")
    write_json(output / "report.json", report)
    images = {}
    try:
        for name, roughness in CASES:
            folder = output / name
            fixture(folder, roughness)
            evidence, image = capture(exe, folder, args.samples, args.timeout)
            evidence.update(name=name, roughness=roughness, inputSha256={p.name: sha(p) for p in
                (folder / "scene.json", folder / "render-preset.json", folder / "target.gltf")})
            report["records"].append(evidence)
            images[name] = image
            # Fixed linear display scale, no per-image normalization or tone mapping.
            Image.fromarray(np.uint8(np.clip(image[:, :, :3], 0, 1) * 255)).save(folder / "linear.png")
            write_json(output / "report.json", report)
            print("Captured " + name, flush=True)
        report["assessment"] = assess(images)
        report["status"] = "diagnostic-complete"
    except Exception as error:
        report.update(status="failed", failure=str(error))
    write_json(output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("status", "assessment", "failure") if k in report}, indent=2))
    return report["status"] == "failed"


if __name__ == "__main__":
    raise SystemExit(main())
