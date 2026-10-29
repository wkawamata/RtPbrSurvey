"""Inspect Native PT helmet reflections without using Hybrid as ground truth."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess

from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "Assets/Scene/PathTracingValidation/32-helmet-reflection/scene.json"
ASSET = ROOT / "Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"
CASES = (
    ("original-4", 4, False, False),
    ("no-normal-4", 4, True, False),
    ("black-ground-4", 4, False, True),
    ("original-2", 2, False, False),
    ("original-8", 8, False, False),
)


def make_case(folder, bounces, remove_normal, black_ground):
    scene = json.loads(SCENE.read_text(encoding="utf-8"))
    preset = json.loads(SCENE.with_name("render-preset.json").read_text(encoding="utf-8"))
    asset = json.loads(ASSET.read_text(encoding="utf-8"))
    for item in asset.get("buffers", []) + asset.get("images", []):
        if "uri" in item and not item["uri"].startswith("data:"):
            item["uri"] = Path(os.path.relpath((ASSET.parent / item["uri"]).resolve(), folder)).as_posix()
    if remove_normal:
        for material in asset["materials"]:
            material.pop("normalTexture", None)
    if black_ground:
        scene["environment"]["groundColor"] = [0, 0, 0]
    scene["assets"][0]["path"] = "helmet.gltf"
    preset["pathTracing"]["maxBounces"] = bounces
    for name, value in (("scene.json", scene), ("render-preset.json", preset), ("helmet.gltf", asset)):
        write_json(folder / name, value)
    return scene, preset


def validate_state(state, meta, bounces, samples):
    expected = dict(scene="PT Helmet Reflection", reconstructionRequested="native", reconstructionOutput="native",
                    environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
                    emissiveEnabled=False, nrdPreviewRequested=False, maxBounces=bounces,
                    accumulatedSamples=samples, randomSeed=7, samplesPerFrame=1, pixelCenterPrimary=True,
                    d3d12DebugLayerEnabledByApp=True)
    for key, value in expected.items():
        if type(state.get(key)) is not type(value) or state[key] != value:
            raise ValueError("Runtime mismatch: " + key)
    if (meta.get("resource") != "PathTracing.SceneColor" or meta.get("primarySampling") != "pixel-center"
            or meta.get("samplesPerFrame") != 1):
        raise ValueError("Wrong captured resource or primary sampling")


def difference(a, b):
    import numpy as np
    if a.shape != b.shape or not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("Invalid paired capture arrays")
    delta = np.abs(a[..., :3] - b[..., :3])
    return dict(maxAbsolute=float(delta.max()), meanAbsolute=float(delta.mean()),
                changedPixels=int(np.any(delta > 0, axis=2).sum()), exactEqual=bool(np.array_equal(a, b)))


def main():
    import numpy
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/x64/Debug/RtPbrSurvey.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "bin/PathTracingValidation/helmet-reflection")
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if args.samples < 1 or args.timeout < 1:
        parser.error("Samples and timeout must be positive")
    args.exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    asset = json.loads(ASSET.read_text(encoding="utf-8"))
    inputs = [ASSET, SCENE, SCENE.with_name("render-preset.json"), Path(__file__).resolve()]
    inputs += [ASSET.parent / x["uri"] for x in asset.get("buffers", []) + asset.get("images", [])
               if "uri" in x and not x["uri"].startswith("data:")]
    report = dict(schemaVersion=1, status="running", generatedUtc=datetime.now(timezone.utc).isoformat(),
                  testedCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                  executableSha256=sha(args.exe), inputSha256={str(p.relative_to(ROOT)): sha(p) for p in inputs},
                  sourceSha256={p: sha(ROOT / p) for p in ("GltfLoader.cpp", "Shaders/SceneRayQuery.hlsli",
                      "Shaders/shaders_PathTracing.hlsl")},
                  expectedCases=[c[0] for c in CASES], samples=args.samples, records=[],
                  qualityAccepted=False, comparisonDomain="full-frame linear HDR, including background",
                  scope="Native PT diagnostic; not Hybrid or RR quality qualification",
                  assetHasNormalTexture=any("normalTexture" in m for m in asset["materials"]),
                  assetHasTangents=any("TANGENT" in p["attributes"] for m in asset["meshes"] for p in m["primitives"]))
    images = {}
    write_json(output / "report.json", report)
    try:
        for name, bounces, remove_normal, black_ground in CASES:
            folder = output / name
            folder.mkdir(parents=True, exist_ok=True)
            make_case(folder, bounces, remove_normal, black_ground)
            path, log = folder / "capture.ptbuf", folder / "debug.log"
            path.unlink(missing_ok=True)
            log.unlink(missing_ok=True)
            command = [str(args.exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
                       "-PathTracingReconstruction", "native", "-WindowSize", "640", "360",
                       "-PathTracingSamples", str(args.samples), "-DebugPreviewResource", "PathTracing.SceneColor",
                       "-CapturePath", str(path), "-LogToFile", str(log), "-ExitAfterCapture"]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            with (folder / "process.log").open("wb") as stream:
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, stdout=stream,
                                         stderr=subprocess.STDOUT)
                try:
                    code = child.wait(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                    raise ValueError("Capture timed out: " + name)
            text = log.read_text(encoding="utf-8-sig")
            states = [json.loads(s[len("[PathTracing] "):]) for s in text.splitlines()
                      if s.startswith("[PathTracing] ")]
            if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(states) != 1:
                raise ValueError("Runtime/Debug Layer failure: " + name)
            meta, image = read_buffer(path)
            validate_state(states[0], meta, bounces, args.samples)
            images[name] = image.copy()
            report["records"].append(dict(name=name, command=command, runtime=states[0], metadata=meta,
                captureSha256=sha(path), logSha256=sha(log), inputSha256={p.name: sha(p) for p in
                    (folder / "scene.json", folder / "render-preset.json", folder / "helmet.gltf")},
                d3d12Errors=0))
            write_json(output / "report.json", report)
            print("Captured " + name, flush=True)
        report["comparisons"] = {name: difference(images["original-4"], images[name])
                                 for name in images if name != "original-4"}
        report["status"] = "diagnostic-complete"
    except Exception as error:
        report.update(status="failed", failure=str(error))
    write_json(output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("status", "comparisons", "failure") if k in report}, indent=2))
    return report["status"] == "failed"


if __name__ == "__main__":
    raise SystemExit(main())
