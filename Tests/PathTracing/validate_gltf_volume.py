"""RAW paired-path checks for the saved convex glTF volume fixture."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np

from transmission_texture_fixture import ROOT, write_json
from validate_part1 import sha
from validate_volume_absorption import expected_transmittance
from volume_capture import capture_volume
from validate_volume_boundary import reference_rays


def reference_tetra_lengths(eye, directions):
    normals = np.array([[-1, 0, 0], [0, -1, 0], [0, 0, 1], [1, 1, -1]], dtype=float)
    lower = np.zeros(directions.shape[:-1])
    upper = np.full_like(lower, np.inf)
    valid = np.ones_like(lower, dtype=bool)
    for normal in normals:
        denominator = np.sum(directions*normal, axis=-1)
        numerator = .5-np.dot(eye, normal)
        parallel = np.abs(denominator) < 1e-12
        valid &= ~parallel | (numerator >= 0)
        crossing = np.divide(numerator, denominator, out=np.zeros_like(lower), where=~parallel)
        lower = np.where(denominator < -1e-12, np.maximum(lower, crossing), lower)
        upper = np.where(denominator > 1e-12, np.minimum(upper, crossing), upper)
    valid &= upper > lower
    return np.where(valid, upper-lower, 0), valid, upper-lower


def rays_at_pixels(scene, height, width, y, x):
    eye = np.array(scene["camera"]["position"], dtype=float)
    forward = np.array(scene["camera"]["target"], dtype=float)-eye
    forward /= np.linalg.norm(forward)
    right = np.cross(scene["camera"]["up"], forward)
    right /= np.linalg.norm(right)
    up = np.cross(forward, right)
    tangent = np.tan(np.radians(scene["camera"]["verticalFovDegrees"])/2)
    directions = forward+right*((2*(x+.5)/width-1)*tangent*width/height)[..., None]
    directions += up*((1-2*(y+.5)/height)*tangent)[..., None]
    directions /= np.linalg.norm(directions, axis=-1)[..., None]
    return eye, directions


def assess_status(status, scene, preset):
    invalid = np.any(status[..., :2] != 0, axis=-1)
    y, x = np.where(invalid)
    if not len(x):
        return dict(grazingMissPixels=0)
    if not np.all(status[invalid][..., :2] == [1, 1]):
        raise ValueError("Unexpected volume status outside the known missed-exit condition")
    eye, rays = rays_at_pixels(scene, *status.shape[:2], y, x)
    _, _, gap = reference_tetra_lengths(eye, rays)
    roundoff = float(32*np.finfo(np.float32).eps*max(1, float(np.max(np.abs(eye)))))
    offset = preset["shadow"]["normalBias"]+preset["shadow"]["rayTMin"]
    if not np.isfinite(gap).all() or np.any(gap > offset+roundoff) or np.any(gap < -roundoff):
        raise ValueError("Volume missed-exit status outside the analytic ray-offset silhouette band")
    return dict(grazingMissPixels=len(x), maximumGrazingLength=float(gap.max()),
                offsetFloor=offset, numericalMargin=roundoff)


def assess_thin_paths(distance, transmittance, color, attenuation_distance):
    if (distance.shape != transmittance.shape or distance.ndim != 3 or distance.shape[-1] != 3
            or not np.isfinite(distance).all() or not np.isfinite(transmittance).all()):
        raise ValueError("Invalid thin-path capture arrays")
    thin = (distance[..., 0] == 1) & (distance[..., 1] == 1) & (distance[..., 2] > 0) & (distance[..., 2] <= .001)
    if thin.sum() < 100:
        raise ValueError("Missing complete thin silhouette paths (<=0.001 world unit) in the fixed fixture")
    expected = expected_transmittance(distance[..., 2][thin], color, attenuation_distance)
    error = float(np.max(np.abs(transmittance[thin]-expected)))
    if error > .002:
        raise ValueError(f"Thin silhouette absorption mismatch: {error}")
    return dict(thinCompletePixels=int(thin.sum()), thinAbsorptionError=error, thinAbsorptionTolerance=.002)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-grazing-pixels", type=int, default=0)
    parser.add_argument("--rr-bypass", action="store_true", help="Also verify RR refusal in an RR-enabled build")
    args = parser.parse_args()
    if args.max_grazing_pixels < 0:
        parser.error("--max-grazing-pixels must be nonnegative")
    exe = args.exe.resolve(strict=True)
    source = ROOT/"Assets/Scene/PathTracingValidation/31-gltf-volume-tetra"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    scene = json.loads((source/"scene.json").read_text(encoding="utf-8"))
    preset = json.loads((source/"render-preset.json").read_text(encoding="utf-8"))
    scene["assets"] = [dict(id="solid", type="gltf", path="volume.gltf")]
    scene["nodes"] = scene["nodes"][:1]
    preset["pathTracing"].update(environmentEnabled=False, accumulate=False, maxBounces=16)
    gltf = json.loads((source/"volume.gltf").read_text(encoding="utf-8"))
    material = gltf["materials"][0]["extensions"]
    material["KHR_materials_ior"] = dict(ior=1)
    gltf["extensionsUsed"].append("KHR_materials_ior")
    gltf["extensionsRequired"].append("KHR_materials_ior")
    cases = [("distance", 4, False), ("transmittance", 8, False), ("status", 5, False), ("nrd-bypass", 8, True),
             ("high-bias-status", 5, False), ("high-bias-transmittance", 8, False)]
    if args.rr_bypass:
        cases.append(("rr-bypass", 3, False))
    dependencies = ("volume_capture.py", "validate_volume_absorption.py", "validate_volume_boundary.py",
                    "validate_inputs.py", "validate_part1.py", "transmission_texture_fixture.py")
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    report = dict(status="running", expectedCases=[name for name, _, _ in cases], cases=[], failures=[],
                  executableSha256=sha(exe), shaderSha256=sha(shader), runnerSha256=sha(Path(__file__)),
                  dependencySha256={name: sha(Path(__file__).with_name(name)) for name in dependencies},
                  grazingPixelBudget=args.max_grazing_pixels, rrBypassRequested=args.rr_bypass)
    patches = {}
    try:
        for name, debug, nrd in cases:
            folder = output/name
            folder.mkdir()
            settings = copy.deepcopy(preset)
            settings["pathTracing"]["debugOutput"] = debug
            if name.startswith("high-bias-"):
                settings["shadow"].update(normalBias=.01, rayTMin=.01)
            write_json(folder/"scene.json", scene)
            write_json(folder/"render-preset.json", settings)
            write_json(folder/"volume.gltf", gltf)
            evidence, image = capture_volume(exe, folder, 1, "CurrentFrameRadiance", nrd, rr=name == "rr-bypass")
            if not np.isfinite(image).all():
                raise ValueError("Nonfinite RAW capture")
            patches[name] = image[..., :3].astype(float)
            report["cases"].append(dict(case=name, evidence=evidence))
        distance = patches["distance"]
        eye, directions, roi = reference_rays(scene, *distance.shape[:2])
        expected_length, valid, _ = reference_tetra_lengths(eye, directions)
        if not valid.all():
            raise ValueError("Fixed central ROI must lie inside the analytic tetrahedron silhouette")
        reference_error = float(np.max(np.abs(distance[roi][..., 2]-expected_length)))
        if reference_error > .005 or not np.all(distance[roi][..., :2] == 1):
            raise ValueError(f"Imported tetrahedron geometric path length mismatch: {reference_error}")
        mask = (distance[..., 0] == 1) & (distance[..., 1] == 1) & (distance[..., 2] > .05)
        if mask.sum() < 100:
            raise ValueError("Missing complete imported-volume entry/exit paths")
        status_assessment = assess_status(patches["status"], scene, preset)
        if status_assessment["grazingMissPixels"] > args.max_grazing_pixels:
            raise ValueError("Grazing missed-exit count exceeds the declared improvement budget")
        if np.any(patches["high-bias-status"][..., :2] != 0):
            raise ValueError("High-bias control reported an error or unfinished medium")
        bias_error = float(np.max(np.abs(patches["high-bias-transmittance"]-patches["transmittance"])))
        if bias_error != 0:
            raise ValueError("Volume absorption changed with bias/TMin x100")
        medium = material["KHR_materials_volume"]
        expected = expected_transmittance(distance[..., 2][mask], medium["attenuationColor"], medium["attenuationDistance"])
        error = float(np.max(np.abs(patches["transmittance"][mask]-expected)))
        thin_assessment = assess_thin_paths(distance, patches["transmittance"], medium["attenuationColor"], medium["attenuationDistance"])
        bypass_error = float(np.max(np.abs(patches["nrd-bypass"]-patches["transmittance"])))
        if error > .002 or bypass_error != 0:
            raise ValueError(f"Imported absorption/bypass mismatch: interior={error}, bypass={bypass_error}")
        if [case["case"] for case in report["cases"]] != report["expectedCases"] or sha(exe) != report["executableSha256"] or sha(shader) != report["shaderSha256"]:
            raise ValueError("Missing cases or binary changed during captures")
        result = "passed-gltf-volume-with-grazing-limit" if status_assessment["grazingMissPixels"] else "passed-gltf-volume-paired-paths"
        report.update(status=result, assessment=dict(completePixels=int(mask.sum()),
                      maximumAbsorptionError=error, absorptionTolerance=.002, nrdBypassError=bypass_error,
                      geometricLengthError=reference_error, geometricLengthTolerance=.005,
                      status=status_assessment, highBiasTransmittanceError=bias_error,
                      **thin_assessment))
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output/"report.json", report)
    print(report["status"])


if __name__ == "__main__":
    main()
