"""Audit RELAX camera history, SDK matrix conventions and screen-space motion."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer, project
from validate_secondary_geometry import primary_rays, planes_from_scene, intersect_planes
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]


def assess_frame(meta, continuous, moving):
    state = meta["nrdLastEvaluation"]
    if state["continuousCameraHistory"] is not continuous or state["dispatchCount"] <= 0:
        raise ValueError("Missing or wrong NRD evaluation mode")
    expected_reset = moving and not continuous
    if state["reset"] is not expected_reset or (state["frameIndex"] == 0) != expected_reset:
        raise ValueError("NRD history unexpectedly restarted or failed to restart")
    if not expected_reset and state["frameIndex"] < 20:
        raise ValueError("Insufficient consecutive NRD history")
    if state["motionVectorScale"] != [0.5, -0.5, 0.0]:
        raise ValueError("SDK screen-motion convention changed")
    for prior in (False, True):
        prefix = "previous" if prior else ""
        view = np.asarray(state[prefix + "WorldToView" if prior else "worldToView"]).reshape(4, 4)
        projection = np.asarray(state[prefix + "ViewToClip" if prior else "viewToClip"]).reshape(4, 4)
        observed = np.asarray(meta["viewProjection" if not prior or expected_reset else "previousViewProjection"])
        if not np.isfinite(view).all() or not np.isfinite(projection).all() or not np.allclose(
                (view @ projection).T, observed, atol=2e-6, rtol=2e-6):
            raise ValueError("SDK camera matrices disagree with the actual MV basis")
    return dict(frameIndex=state["frameIndex"], reset=state["reset"], dispatchCount=state["dispatchCount"])


def assess_motion(meta, image, scene, moving):
    if meta["resource"] != "PathTracing.MotionVectors" or meta["format"] != 34:
        raise ValueError("Expected RG16 native motion")
    if image.shape != (meta["height"], meta["width"], 2) or not np.isfinite(image).all():
        raise ValueError("Malformed motion image")
    ys, xs = np.mgrid[16:meta["height"]-16:8, 16:meta["width"]-16:8]
    xs, ys = xs.ravel(), ys.ravel()
    origins, directions = primary_rays(meta, xs, ys)
    distances, indices, edges = intersect_planes(origins, directions, planes_from_scene(scene), meta["rayTMin"], meta["rayTMax"])
    valid = (indices >= 0) & ~edges
    if valid.sum() < 100:
        raise ValueError("Insufficient independent primary-hit reference")
    world = origins[valid] + directions[valid] * distances[valid, None]
    expected = (project(world, meta["previousViewProjection"]) - project(world, meta["viewProjection"]))[:, :2]
    observed = image[ys[valid], xs[valid]]
    bound = 2*np.abs(np.spacing(np.abs(expected).astype(np.float16)).astype(float)) + 3e-6
    error = abs(observed - expected)
    if not (error <= bound).all():
        raise ValueError("Motion failed the existing format-derived bound")
    magnitude = float(np.max(abs(observed)))
    if moving and magnitude < 1e-5:
        raise ValueError("Orbit produced no measurable motion")
    if not moving and meta["viewProjection"] != meta["previousViewProjection"]:
        raise ValueError("Static camera changed its projection basis")
    scale = np.asarray(meta["nrdLastEvaluation"]["motionVectorScale"][:2])
    expected_uv = expected * [0.5, -0.5]
    actual_uv = observed * scale
    if not (abs(actual_uv-expected_uv) <= bound * 0.5).all():
        raise ValueError("NRD UV reprojection sign/scale mismatch")
    return dict(pixelCount=int(valid.sum()), maxAbsoluteError=float(error.max()),
        maxBoundNormalizedError=float((error/bound).max()),
        bound="2 ULP(expected half) + 3e-6 NDC", maximumMotionNdc=magnitude,
        expectedUvMean=expected_uv.mean(axis=0).tolist(), observedUvMean=actual_uv.mean(axis=0).tolist())


def capture(folder, name, resource, angle, continuous):
    exe = ROOT / "build/Debug/RtPbrSurvey.exe"
    path, log = folder / (name + ".ptbuf"), folder / (name + ".log")
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-RenderPreset", str(folder / "preset.json"),
        "-EnablePathTracing", "-DebugPreviewResource", "PathTracing." + resource,
        "-CapturePath", str(path), "-CaptureAfterFrames", "30", "-LogToFile", str(log), "-ExitAfterCapture"]
    if angle:
        command += ["-ReflectionOrbitDegrees", str(angle), "-ReflectionOrbitFrames", "20"]
    if continuous:
        command += ["-NrdContinuousCameraHistory"]
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=180)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0 or any(token in log.read_text(encoding="utf-8-sig") for token in ("[ERROR]", "[CORRUPTION]")):
        raise ValueError("Runtime capture failed: " + name)
    meta, image = read_buffer(path)
    if meta["resource"] != "PathTracing." + resource:
        raise ValueError("Wrong captured resource")
    return meta, image, dict(name=name, resource=resource, command=command,
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, folder = args.source.resolve(strict=True), args.output.resolve()
    if source == folder or source in folder.parents or (folder.exists() and any(folder.iterdir())):
        parser.error("Output must be a separate empty directory")
    scene = json.loads((source / "scene.json").read_text())
    preset_path = source / ("preset.json" if (source / "preset.json").exists() else "on.json")
    preset = json.loads(preset_path.read_text())
    preset["pathTracing"].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True, denoiserDistanceDiagnostics=True)
    write_json(folder / "scene.json", scene)
    write_json(folder / "preset.json", preset)
    shader_manifest = ROOT / "build/shaders/nrd-relax-manifest.txt"
    report = dict(status="running", fullIntegrationReady=False, records=[], failures=[],
        exeSha256=sha(ROOT / "build/Debug/RtPbrSurvey.exe"), sceneSha256=sha(folder / "scene.json"),
        presetSha256=sha(folder / "preset.json"), scriptSha256=sha(Path(__file__)),
        shaderManifestSha256=sha(shader_manifest),
        shaders=[dict(identifier=name, sha256=sha(ROOT / f"build/shaders/nrd-relax-{i}.cso"))
                 for i, name in enumerate(shader_manifest.read_text().splitlines())])
    write_json(folder / "report.json", report)
    try:
        outputs = {}
        native_cohorts = {}
        for case, angle, continuous in (("static", 0, True), ("positive", 4, True), ("negative", -4, True),
                                         ("reset-control", 4, False)):
            resources = ("MotionVectors", "NrdOutputDiffuse", "NrdOutputSpecular")
            cohort = None
            for resource in resources:
                meta, image, record = capture(folder, case + "-" + resource, resource, angle, continuous)
                report["records"].append(record)
                comparable = {key: value for key, value in meta.items() if key not in ("resource", "format")}
                if cohort is not None and comparable != cohort:
                    raise ValueError("Mismatched camera capture cohort")
                cohort = comparable
                record["metadata"] = meta
                record["evaluation"] = assess_frame(meta, continuous, angle != 0)
                if resource == "MotionVectors":
                    record["assessment"] = assess_motion(meta, image, scene, angle != 0)
                else:
                    if meta["format"] != 2 or not np.isfinite(image).all() or (image < 0).any() or not (image[..., :3] > 0).any():
                        raise ValueError("Invalid RELAX motion output")
                    outputs[case, resource] = image
                write_json(folder / "report.json", report)
            native_cohorts[case] = {key: value for key, value in cohort.items() if key != "nrdLastEvaluation"}
        if native_cohorts["positive"] != native_cohorts["reset-control"]:
            raise ValueError("History control did not use identical native camera/sample inputs")
        report["historyEffect"] = {}
        for resource in ("NrdOutputDiffuse", "NrdOutputSpecular"):
            continued, reset = outputs["positive", resource], outputs["reset-control", resource]
            changed = int(np.any(continued[..., :3] != reset[..., :3], axis=-1).sum())
            if changed < 100:
                raise ValueError("Continued history had no measurable output effect")
            report["historyEffect"][resource] = dict(changedPixels=changed)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", failures=[str(error)])
    report["limitation"] = ("One static planar fixture, +/-4 degree camera orbit, native resolution. "
        "Reset control also restarts SDK frame index; RGB differences are a combined policy effect, not an isolated temporal-reuse oracle. "
        "No visual ghosting or disocclusion quality acceptance.")
    write_json(folder / "report.json", report)
    print(report["status"], report.get("historyEffect"), report["failures"], flush=True)
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
