"""Strict RAW checks of closed-volume boundaries; absorption is deliberately gated."""
import argparse
import json
import math
from pathlib import Path

import numpy as np

from volume_boundary_fixture import write_volume_fixture
from transmission_texture_fixture import BACKGROUND, write_json
from volume_capture import capture_volume
from validate_part1 import sha


def reference_rays(scene, height, width, size=32):
    y, x = np.mgrid[height // 2-size//2:height // 2+size//2, width // 2-size//2:width // 2+size//2]
    eye = np.array(scene["camera"]["position"], dtype=float)
    forward = np.array(scene["camera"]["target"], dtype=float) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(scene["camera"]["up"], forward)
    right /= np.linalg.norm(right)
    up = np.cross(forward, right)
    tangent = math.tan(math.radians(40) / 2)
    direction = forward + right * ((2*(x+.5)/width-1)*tangent*width/height)[..., None]
    direction += up * ((1-2*(y+.5)/height)*tangent)[..., None]
    direction /= np.linalg.norm(direction, axis=-1)[..., None]
    return eye, direction, (slice(int(y.min()), int(y.max()+1)), slice(int(x.min()), int(x.max()+1)))


def assess(image, scene, name, ior):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid volume RAW image")
    eye, direction, roi = reference_rays(scene, *image.shape[:2], size=4 if name in ("exit-position", "tir-state") else 32)
    patch = image[roi][..., :3]
    cosine = direction[..., 2]
    inside = direction / ior
    inside[..., 2] = np.sqrt(1-np.sum(inside[..., :2]**2, axis=-1))
    expected_length = 1 / inside[..., 2]
    if name == "sphere-counts":
        projection = np.sum(eye * direction, axis=-1)
        expected_length = 2*np.sqrt(projection**2-(np.dot(eye, eye)-1))
    if name.endswith("counts"):
        expected = np.stack((np.ones_like(cosine), np.ones_like(cosine), expected_length), axis=-1)
        error = float(np.max(np.abs(patch-expected)))
        tolerance = .005
    elif name in ("slab-status", "sphere-status"):
        error, tolerance = float(np.max(np.abs(patch))), 1e-7
    elif name == "budget-control":
        error, tolerance = float(np.max(np.abs(patch-[3, 1, 0]))), 1e-7
    elif name == "entry-direction":
        selected = patch[..., 2] > 0
        if float(selected.mean()) < .8:
            raise ValueError("Too few transmitted primary samples")
        error, tolerance = float(np.max(np.abs(patch[selected]-inside[selected]))), .002
    elif name == "exit-position":
        entry = eye + direction * ((-.5-eye[2])/cosine)[..., None]
        expected = entry + inside * expected_length[..., None]
        selected = np.abs(patch[..., 2]-.5) < .002
        if selected.sum() < 12:
            raise ValueError("Too few forward exits in fixed 4x4 ROI")
        error, tolerance = float(np.max(np.abs(patch[selected]-expected[selected]))), .003
    else:
        cosine_t = inside[..., 2]
        rs = (cosine-ior*cosine_t)/(cosine+ior*cosine_t)
        rp = (ior*cosine-cosine_t)/(ior*cosine+cosine_t)
        fresnel = (rs**2+rp**2)/2
        forward_probability = (1-fresnel)/(1+fresnel)
        if name == "outgoing-direction":
            expected = direction.copy()
            expected[..., 2] *= 2*forward_probability-1
            error, tolerance = float(np.max(np.abs(patch.mean(axis=(0, 1))-expected.mean(axis=(0, 1))))), .02
        elif name in ("reflection-state", "tir-state"):
            if float(patch[..., 2].mean()) < (.5 if name == "tir-state" else .005):
                raise ValueError("Internal reflection control has no reflections")
            error, tolerance = float(np.max(np.abs(patch[..., :2]))), 1e-7
        elif name == "nrd-fallback":
            error, tolerance = float(np.max(np.abs(patch-BACKGROUND))), .001
        elif name == "radiance":
            expected = forward_probability[..., None]*np.array(BACKGROUND)
            error, tolerance = float(np.max(np.abs(patch.mean(axis=(0, 1))-expected.mean(axis=(0, 1))))), .015
        else:
            raise ValueError("Unknown volume case")
    if error > tolerance:
        raise ValueError(f"{name}: error={error} budget={tolerance}")
    return dict(error=error, tolerance=tolerance, mean=patch.mean(axis=(0, 1)).tolist(),
        roi=[roi[1].start, roi[0].start, roi[1].stop, roi[0].stop])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = [("slab-counts", 4, 1, False, 1, "SceneColor"), ("slab-status", 5, 1, False, 1, "SceneColor"),
        ("entry-direction", 3, 1.5, False, 1, "PrimaryContinuation"),
        ("exit-position", 7, 1.5, False, 1, "SceneColor"),
        ("outgoing-direction", 6, 1.5, False, 64, "SceneColor"),
        ("reflection-state", 5, 1.5, False, 64, "SceneColor"), ("radiance", 3, 1.5, False, 64, "SceneColor"),
        ("sphere-counts", 4, 1, True, 1, "SceneColor"), ("sphere-status", 5, 1, True, 1, "SceneColor"),
        ("nrd-fallback", 3, 1, False, 8, "SceneColor"), ("budget-control", 5, 1, False, 1, "SceneColor"),
        ("tir-state", 5, 1.5, False, 64, "SceneColor")]
    report = dict(status="running", executableSha256=sha(exe), shaderSha256=sha(shader), runnerSha256=sha(Path(__file__)),
        fixtureSha256=sha(Path(__file__).with_name("volume_boundary_fixture.py")), expectedCases=[c[0] for c in cases], cases=[], failures=[],
        dependencySha256={name: sha(Path(__file__).with_name(name)) for name in
            ("volume_capture.py", "transmission_texture_fixture.py", "validate_inputs.py", "validate_part1.py")})
    write_json(output/"report.json", report)
    try:
        for name, mode, ior, sphere, frames, resource in cases:
            folder = output/name
            scene, preset = write_volume_fixture(folder, mode=mode, ior=ior, sphere=sphere,
                bounces=2 if name == "budget-control" else 16, tir=name == "tir-state")
            evidence, image = capture_volume(exe, folder, frames, resource, nrd=name == "nrd-fallback")
            assessment = assess(image, scene, name, ior)
            report["cases"].append(dict(case=name, assessment=assessment, **evidence))
            write_json(output/"report.json", report)
        if [c["case"] for c in report["cases"]] != report["expectedCases"] or sha(exe) != report["executableSha256"] or sha(shader) != report["shaderSha256"]:
            raise ValueError("Incomplete cases or modified binaries")
        report["status"] = "passed-volume-boundaries"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output/"report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
