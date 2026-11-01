"""Independent Beer-Lambert and multi-segment RAW checks for Native PT volumes."""
import argparse
import copy
from pathlib import Path

import numpy as np

from transmission_texture_fixture import BACKGROUND, write_json
from validate_part1 import sha
from validate_volume_boundary import reference_rays
from volume_absorption_fixture import COLOR, write_absorption_fixture
from volume_capture import capture_volume


def expected_transmittance(length, color, distance):
    length, color = np.asarray(length, dtype=float), np.asarray(color, dtype=float)
    if color.shape != (3,) or not np.isfinite(color).all() or np.any(color < 0) or np.any(color > 1) or not np.isfinite(length).all() or np.any(length < 0):
        raise ValueError("Invalid Beer-Lambert reference input")
    if distance is None or distance == float("inf"):
        return np.ones((*length.shape, 3))
    if not np.isfinite(distance) or distance <= 0:
        raise ValueError("Invalid attenuation distance")
    return np.power(color, length[..., None]/distance)


def assess(image, scene, preset, config, kind, name, pairs):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128 or not np.isfinite(image).all():
        raise ValueError("Invalid absorption RAW image")
    eye, direction, roi = reference_rays(scene, *image.shape[:2], size=4 if config.get("tir") else 32)
    patch = image[roi][..., :3]
    color, distance = config.get("color", COLOR), config.get("distance", 1)
    ior = config.get("ior", 1)
    cosine = direction[..., 2]
    cosine_t = np.sqrt(1-(1-cosine*cosine)/(ior*ior))
    length = config.get("thickness", 1)*config.get("uniform_scale", 1)/cosine_t
    if config.get("sphere"):
        length = 2*np.sqrt(np.sum(eye*direction, axis=-1)**2-(np.dot(eye, eye)-1))
    expected = expected_transmittance(length, color, distance)
    tolerance = .002 if config.get("sphere") else .001
    pair_name = "tir" if config.get("tir") else "reflection"
    extra = {}
    if kind == "distance":
        if np.max(np.abs(patch[..., 0]-patch[..., 1])) != 0 or np.max(patch[..., 2]) < (.5 if config.get("tir") else 1.5):
            raise ValueError("Multi-segment control lacks complete exits or internal distance")
        fingerprint = copy.deepcopy(preset)
        fingerprint["pathTracing"].pop("debugOutput")
        pairs[pair_name] = dict(case=name, length=patch[..., 2].copy(), scene=scene, preset=fingerprint)
        error, tolerance = 0.0, 0.0
        extra["maximumLength"] = float(patch[..., 2].max())
    elif kind == "paired-transmittance":
        if pair_name not in pairs:
            raise ValueError("Missing paired path-length evidence")
        pair = pairs[pair_name]
        fingerprint = copy.deepcopy(preset)
        fingerprint["pathTracing"].pop("debugOutput")
        if scene != pair["scene"] or fingerprint != pair["preset"]:
            raise ValueError("Paired paths have different scene or sampling settings")
        expected = expected_transmittance(pair["length"], color, distance)
        error, tolerance = float(np.max(np.abs(patch-expected))), .002
        extra["pairedCase"] = pair["case"]
    elif kind == "status":
        if float(patch[..., 2].mean()) < (.5 if config.get("tir") else .005):
            raise ValueError("Absorbing reflection control has no internal reflections")
        error, tolerance = float(np.max(np.abs(patch[..., :2]))), 1e-7
    elif kind == "fresnel":
        rs = (cosine-ior*cosine_t)/(cosine+ior*cosine_t)
        rp = (ior*cosine-cosine_t)/(ior*cosine+cosine_t)
        reflection = (rs*rs+rp*rp)/2
        expected = np.array(BACKGROUND)*(1-reflection[..., None])**2*expected / (1-reflection[..., None]**2*expected**2)
        error, tolerance = float(np.max(np.abs(patch.mean(axis=(0, 1))-expected.mean(axis=(0, 1))))), .015
    else:
        if kind == "radiance":
            expected *= np.array(BACKGROUND)
        elif kind != "transmittance":
            raise ValueError("Unknown absorption assessment kind")
        error = float(np.max(np.abs(patch-expected)))
    if error > tolerance:
        raise ValueError(f"{name}: error={error}, budget={tolerance}")
    return dict(error=error, tolerance=tolerance, mean=patch.mean(axis=(0, 1)).tolist(),
        roi=[roi[1].start, roi[0].start, roi[1].stop, roi[0].stop], **extra), patch.copy()


def assess_relationships(patches):
    required = ("normal", "double", "oblique", "bias-control", "nrd-fallback")
    if any(name not in patches for name in required):
        raise ValueError("Missing absorption relationship cases")
    single = patches["normal"]/np.array(BACKGROUND)
    double = patches["double"]/np.array(BACKGROUND)
    errors = dict(doubleThickness=float(np.max(np.abs(double-single**2))),
        biasInvariant=float(np.max(np.abs(patches["bias-control"]-patches["oblique"]))),
        nrdInvariant=float(np.max(np.abs(patches["nrd-fallback"]-patches["normal"]))))
    if errors["doubleThickness"] > .002 or errors["biasInvariant"] > .0005 or errors["nrdInvariant"] != 0:
        raise ValueError("Absorption thickness/bias/NRD relationship failed: "+str(errors))
    return dict(errors=errors, tolerances=dict(doubleThickness=.002, biasInvariant=.0005, nrdInvariant=0))


def capture_resource(kind):
    return "SceneColor" if kind == "fresnel" else "CurrentFrameRadiance"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = [("normal", {}, "radiance"), ("double", dict(thickness=2), "radiance"),
        ("oblique", dict(oblique=True), "radiance"), ("uniform-scale", dict(uniform_scale=2), "radiance"),
        ("black-channel", dict(color=[0, .5, 1]), "radiance"), ("infinite", dict(distance=None, color=[0, .5, 1]), "radiance"),
        ("white", dict(color=[1, 1, 1]), "radiance"), ("sphere", dict(sphere=True), "radiance"),
        ("bias-control", dict(oblique=True, bias=.01), "radiance"),
        ("transmittance", dict(oblique=True, debug=8), "transmittance"),
        ("reflection-distance", dict(ior=1.5, debug=4), "distance"),
        ("reflection-transmittance", dict(ior=1.5, debug=8), "paired-transmittance"),
        ("reflection-status", dict(ior=1.5, debug=5), "status"),
        ("tir-distance", dict(ior=1.5, debug=4, tir=True), "distance"),
        ("tir-transmittance", dict(ior=1.5, debug=8, tir=True), "paired-transmittance"),
        ("tir-status", dict(ior=1.5, debug=5, tir=True), "status"),
        ("fresnel-radiance", dict(ior=1.5, oblique=True), "fresnel"), ("nrd-fallback", {}, "radiance")]
    dependencies = ("volume_capture.py", "volume_absorption_fixture.py", "volume_boundary_fixture.py",
        "validate_volume_boundary.py", "transmission_texture_fixture.py", "validate_inputs.py", "validate_part1.py")
    report = dict(status="running", executableSha256=sha(exe), shaderSha256=sha(shader), runnerSha256=sha(Path(__file__)),
        expectedCases=[c[0] for c in cases], cases=[], failures=[], numpyVersion=np.__version__,
        dependencySha256={name: sha(Path(__file__).with_name(name)) for name in dependencies})
    write_json(output/"report.json", report)
    pairs, patches = {}, {}
    try:
        for name, config, kind in cases:
            folder = output/name
            scene, preset = write_absorption_fixture(folder, **config)
            frames = 64 if kind == "fresnel" else 1
            resource = capture_resource(kind)
            evidence, image = capture_volume(exe, folder, frames, resource=resource, nrd=name == "nrd-fallback")
            assessment, patch = assess(image, scene, preset, config, kind, name, pairs)
            patches[name] = patch
            report["cases"].append(dict(case=name, config=config, warmUpFrames=frames, quantity=resource,
                assessment=assessment, **evidence))
            write_json(output/"report.json", report)
        report["relationships"] = assess_relationships(patches)
        if [c["case"] for c in report["cases"]] != report["expectedCases"] or sha(exe) != report["executableSha256"] or sha(shader) != report["shaderSha256"]:
            raise ValueError("Incomplete cases or modified binaries")
        report["status"] = "passed-volume-absorption"
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output/"report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
