"""Independent double-precision tetrahedron Snell/Fresnel path reference."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np

from transmission_texture_fixture import ROOT, write_json
from validate_gltf_volume import rays_at_pixels
from validate_part1 import sha
from volume_capture import capture_volume


def hash_uint(value):
    value &= 0xffffffff
    value = ((value ^ (value >> 16)) * 0x7feb352d) & 0xffffffff
    value = ((value ^ (value >> 15)) * 0x846ca68b) & 0xffffffff
    return value ^ (value >> 16)


def reference(eye, direction, x, y, seed=7, sample_index=0, angle=0):
    state = hash_uint(int(x) ^ hash_uint(int(y) ^ hash_uint(sample_index) ^ hash_uint(seed + 0x9e3779b9)))

    def random():
        nonlocal state
        state = hash_uint(state + 0x9e3779b9)
        return (state >> 8) / 16777216

    random()
    random()
    normals = np.array([[-1, 0, 0], [0, -1, 0], [0, 0, 1], [1, 1, -1]], dtype=float)
    radians = np.radians(angle)
    rotation = np.array([[np.cos(radians), 0, -np.sin(radians)], [0, 1, 0],
        [np.sin(radians), 0, np.cos(radians)]])
    normals = normals @ rotation
    origin, direction = eye.copy(), direction.copy()
    inside, previous = False, -1
    entries, exits, length, reflections = 0, 0, 0., 0
    outgoing = np.zeros(3)
    for bounce in range(16):
        denom = normals @ direction
        times = np.divide(.5 - normals @ origin, denom, out=np.full(4, np.inf), where=np.abs(denom) > 1e-12)
        hits = []
        for face, t in enumerate(times):
            if face == previous or t < -1e-9 or not np.isfinite(t):
                continue
            point = origin + t * direction
            if np.all(normals @ point <= .5 + 1e-8):
                hits.append((max(0., t), face))
        if not hits:
            raise ValueError("CPU reference unexpectedly missed the closed solid")
        t, face = min(hits)
        point = origin + t * direction
        if inside:
            length += t
        if bounce == 15:
            return np.array([entries, exits, length]), np.array([3, int(inside), reflections]), outgoing
        n = normals[face] / np.linalg.norm(normals[face])
        n = -n if inside else n
        ni, nt = (1.5, 1.) if inside else (1., 1.5)
        c = np.clip(-np.dot(n, direction), 0, 1)
        eta = ni / nt
        s2 = eta**2 * (1-c*c)
        ct = np.sqrt(max(0., 1-s2))
        fresnel = 1. if s2 >= 1 else .5 * (((nt*c-ni*ct)/(nt*c+ni*ct))**2 +
            ((ni*c-nt*ct)/(ni*c+nt*ct))**2)
        reflected = random() < fresnel
        if reflected:
            direction = direction - 2*np.dot(direction, n)*n
            if inside:
                reflections += 1
        else:
            direction = eta*direction + (eta*c-ct)*n
            if inside:
                exits += 1
            else:
                entries += 1
            inside = not inside
        if not inside:
            outgoing = direction.copy()
            return np.array([entries, exits, length]), np.array([0, 0, reflections]), outgoing
        origin, previous = point, face
    raise AssertionError("Unreachable path end")


def run(exe, output, angle=0):
    output.mkdir(parents=True, exist_ok=False)
    source = ROOT / "Assets/Scene/PathTracingValidation/35-gltf-rough-volume-tetra"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((source / scene["renderPreset"]).read_text(encoding="utf-8"))
    gltf = json.loads((source / "volume.gltf").read_text(encoding="utf-8"))
    gltf["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = 0
    scene["nodes"], scene["assets"] = scene["nodes"][:1], scene["assets"][:1]
    scene["renderPreset"] = "render-preset.json"
    scene.pop("turntable", None)
    radians = np.radians(angle)
    scene["nodes"][0]["rotation"] = [0, float(np.sin(radians/2)), 0, float(np.cos(radians/2))]
    scene["nodes"][0]["translation"] = [float(-.5*np.cos(radians)+.5*np.sin(radians)), -.5,
        float(.5*np.sin(radians)+.5*np.cos(radians))]
    preset["pathTracing"].update(environmentEnabled=False, accumulate=False, maxBounces=16)
    report = dict(status="running", captures=[], runnerSha256=sha(Path(__file__)), executableSha256=sha(exe))
    write_json(output / "report.json", report)
    try:
        images = []
        for name, mode in (("distance", 4), ("status", 5), ("outgoing", 6)):
            folder = output / name
            folder.mkdir()
            settings = copy.deepcopy(preset)
            settings["pathTracing"]["debugOutput"] = mode
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", settings)
            write_json(folder / "volume.gltf", gltf)
            evidence, image = capture_volume(exe, folder, 1, "CurrentFrameRadiance")
            images.append(image)
            report["captures"].append(dict(name=name, **evidence))
        h, w = images[0].shape[:2]
        y, x = np.mgrid[h//2-16:h//2+16, w//2-16:w//2+16]
        eye, rays = rays_at_pixels(scene, h, w, y, x)
        sample_index = report["captures"][0]["metadata"]["sampleStartIndex"]
        if any(capture["metadata"]["sampleStartIndex"] != sample_index for capture in report["captures"]):
            raise ValueError("Paired captures used different random sequence indices")
        expected = [reference(eye, ray, px, py, sample_index=sample_index, angle=angle)
            for ray, px, py in zip(rays.reshape(-1, 3), x.ravel(), y.ravel())]
        errors = []
        for index, image in enumerate(images):
            gpu = image[y, x, :3].reshape(-1, 3)
            cpu = np.array([result[index] for result in expected])
            if not np.isfinite(gpu).all():
                raise ValueError("Non-finite GPU evidence")
            errors.append(float(np.max(np.abs(gpu-cpu))))
        report["assessment"] = dict(pixels=x.size, sampleIndex=sample_index, angle=angle,
            errors=errors, tolerances=[.005, 0, .002])
        if any(error > tolerance for error, tolerance in zip(errors, [.005, 0, .002])):
            raise ValueError("CPU/GPU Snell-Fresnel path mismatch: " + str(errors))
        report["status"] = "passed-smooth-volume-paths"
    except Exception as error:
        report.update(status="failed", failure=str(error))
        raise
    finally:
        write_json(output / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--angle", type=float, default=0)
    args = parser.parse_args()
    run(args.exe.resolve(strict=True), args.output.resolve(), args.angle)
