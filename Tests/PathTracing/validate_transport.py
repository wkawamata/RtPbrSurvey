"""Measure GGX sampler agreement and RR mean preservation in linear HDR."""
import argparse
import copy
import json
import math
from pathlib import Path
import statistics
import subprocess
from types import SimpleNamespace

from compare_hdr import build_capture_command, capture
from validate_part1 import write_json, sha

ROOT = Path(__file__).resolve().parents[2]


def hemisphere_integral(view_cosine, albedo, metallic, roughness, order=128):
    import numpy as np
    if not 0 < view_cosine <= 1:
        raise ValueError("View must be above the surface")
    nodes, weights = np.polynomial.legendre.leggauss(order)
    mu = (nodes[:, None]+1)/2
    phi = (np.arange(order*4)[None, :]+.5) * (2*math.pi/(order*4))
    view_dot_light = math.sqrt(1-view_cosine**2)*np.sqrt(1-mu**2)*np.cos(phi)+view_cosine*mu
    half_length = np.sqrt(2+2*view_dot_light)
    normal_dot_half = (view_cosine+mu)/half_length
    view_dot_half = (1+view_dot_light)/half_length
    alpha_squared = max(roughness**2, .001)**2
    distribution = alpha_squared/(math.pi*(normal_dot_half**2*(alpha_squared-1)+1)**2)
    def smith(cosine):
        return 2*cosine/(cosine+np.sqrt(alpha_squared+(1-alpha_squared)*cosine**2))
    f0 = .04*(1-metallic)+albedo*metallic
    fresnel = f0+(1-f0)*(1-view_dot_half)**5
    brdf = (1-fresnel)*(1-metallic)*albedo/math.pi + distribution*smith(view_cosine)*smith(mu)*fresnel/(4*view_cosine*mu)
    return float(np.sum(brdf*mu*weights[:, None]/2)*(2*math.pi/(order*4)))


def plane_reference(scene, metallic, roughness):
    import numpy as np
    from validate_inputs import srgb_texture_material
    camera = scene["camera"]
    forward = np.array(camera["target"], dtype=float)-camera["position"]
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, camera["up"])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    albedo = float(srgb_texture_material([.5,.5,.5], roughness)[0][0])
    values = {128: [], 256: []}
    for y in (518.5, 539.5, 560.5):
        for x in (938.5, 959.5, 980.5):
            tangent = math.tan(math.radians(camera["verticalFovDegrees"])/2)
            ray = forward+right*(2*x/1920-1)*tangent*1920/1080+up*(1-2*y/1080)*tangent
            ray /= np.linalg.norm(ray)
            for order in values:
                values[order].append(hemisphere_integral(-float(ray[1]), albedo, metallic, roughness, order))
    low, high = [statistics.mean(values[order]) for order in (128, 256)]
    return dict(mean=high, quadratureRelativeChange=abs(low/high-1),
                orders=[128,256], spatialSamples=9, decodedAlbedo=albedo,
                limitation="Nine representative ROI view directions; not an independent material or camera parser.")


def reference_agreement(values, reference):
    mean = statistics.mean(values)
    uncertainty = 4*statistics.stdev(values)/math.sqrt(len(values))/reference["mean"]
    difference = abs(mean/reference["mean"]-1)
    status = ("inconclusive" if uncertainty > .05 or reference["quadratureRelativeChange"] > .002 else
              "passed" if difference <= .02+uncertainty else "failed")
    return dict(status=status, measuredMean=mean, relativeDifference=difference,
                fourStandardErrorRelative=uncertainty, reference=reference)


def paired_agreement(left, right, relative_tolerance=.02, max_uncertainty=.05):
    if len(left) != len(right) or len(left) < 4:
        raise ValueError("At least four paired seed observations are required")
    if not all(math.isfinite(v) for v in left + right):
        raise ValueError("Nonfinite observations")
    reference = statistics.mean(left)
    if reference <= 1e-8:
        raise ValueError("Reference is too dark for a relative comparison")
    differences = [b-a for a, b in zip(left, right)]
    difference = statistics.mean(differences)
    standard_error = statistics.stdev(differences) / math.sqrt(len(differences))
    uncertainty = 4 * standard_error / reference
    relative_difference = abs(difference) / reference
    status = ("inconclusive" if uncertainty > max_uncertainty else
              "passed" if relative_difference <= relative_tolerance + uncertainty else "failed")
    return dict(status=status, referenceMean=reference, candidateMean=statistics.mean(right),
                relativeDifference=relative_difference, pairedStandardError=standard_error,
                fourStandardErrorRelative=uncertainty, relativeTolerance=relative_tolerance,
                maxUncertainty=max_uncertainty)


def room_fixture():
    folder = ROOT / "Assets/Scenes/PathTracingValidation/two-surface-indirect"
    scene = json.loads((folder / "scene.json").read_text())
    preset = json.loads((folder / "render-preset.json").read_text())
    template = scene["nodes"][0]
    scene["materials"] = [dict(id="neutral", name="neutral", baseColor=[.7,.7,.7,1],
                               metallic=0, roughness=.8)]
    scene["nodes"] = []
    for name, position, scale in (
        ("floor", [0,-.1,0], [8,.2,10]), ("ceiling", [0,4.1,0], [8,.2,10]),
        ("left", [-4.1,2,0], [.2,4,10]), ("right", [4.1,2,0], [.2,4,10]),
        ("back", [0,2,5.1], [8,4,.2]), ("front", [0,2,-5.1], [8,4,.2])):
        node = copy.deepcopy(template)
        node.update(id=name, name=name, translation=position, scale=scale)
        node["primitive"].update(kind="cube", size=1)
        scene["nodes"].append(node)
    scene["camera"].update(position=[0,2,-3], target=[0,1,2], nearZ=.01)
    preset["lighting"]["lights"][0].update(position=[0,3,0], intensity=8)
    preset["pathTracing"].update(maxBounces=8, debugOutput=3)
    return scene, preset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--seeds", type=int, nargs="+", default=[11, 23, 37, 53])
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--rr-only", action="store_true", help="Repeat the enclosed-room cohort without GGX captures")
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or args.timeout < 1 or len(set(args.seeds)) < 4 or len(set(args.seeds)) != len(args.seeds):
        parser.error("Positive samples and at least four unique seeds are required")
    if any(seed < 0 or seed > 0xffffffff for seed in args.seeds):
        parser.error("Seeds must be uint32")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Output must be empty; retain failed measurements")
    output.mkdir(parents=True, exist_ok=True)
    report = dict(schemaVersion=1, status="running", commit=subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(), samples=args.samples,
        seeds=args.seeds, rrOnly=args.rr_only, captures=[], comparisons=[], failures=[],
        exeHash=sha(ROOT / "bin/x64/Debug/RtPbrSurvey.exe"),
        sourceHashes={path: sha(ROOT / path) for path in (
            "Tests/PathTracing/validate_transport.py", "Tests/PathTracing/compare_hdr.py",
            "Tests/PathTracing/validate_inputs.py", "Shaders/PathTracingSampling.hlsli",
            "Shaders/shaders_PathTracing.hlsl", "Scene/SceneDocumentBuilder.cpp")},
        limitation="Paired seed-level ROI means; four standard errors are a diagnostic band, not a formal confidence interval. Finite bounce limits. No denoising or tone map. No universal energy conservation claim.")
    report["gpuDriver"] = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version",
        "--format=csv,noheader"], text=True).strip()

    def run(scene, preset, name, mode, roi):
        scene = copy.deepcopy(scene)
        scene_path, preset_path = output / (name+"-scene.json"), output / (name+"-preset.json")
        scene["sceneId"] = name
        scene["renderPreset"] = preset_path.name
        write_json(scene_path, scene)
        write_json(preset_path, preset)
        scene_hash, preset_hash = sha(scene_path), sha(preset_path)
        request = SimpleNamespace(root=ROOT, exe=ROOT / "bin/x64/Debug/RtPbrSurvey.exe",
            scene_file=scene_path, render_preset=preset_path, output=output, timeout=args.timeout, roi=roi)
        values = []
        for seed in args.seeds:
            if (sha(scene_path), sha(preset_path)) != (scene_hash, preset_hash):
                raise ValueError("Fixture changed during measurement")
            capture_name = name+f"-seed-{seed}"
            record, pixels = capture(request, mode, seed, args.samples, capture_name)
            if (sha(scene_path), sha(preset_path)) != (scene_hash, preset_hash):
                raise ValueError("Fixture changed during capture")
            expected = preset["pathTracing"]
            for key, diagnostic_key in (("maxBounces", "maxBounces"),
                                        ("russianRouletteEnabled", "russianRoulette")):
                if record["diagnostics"][diagnostic_key] != expected[key]:
                    raise ValueError(f"Unexpected {key}")
            record.update(case=name, roi=roi, sceneHash=scene_hash, presetHash=preset_hash,
                command=build_capture_command(request, mode, seed, args.samples,
                    output / (capture_name+".pfm"), output / (capture_name+".log")))
            record["roiMeanRgb"] = [sum(pixels[c::3]) / (len(pixels)//3) for c in range(3)]
            report["captures"].append(record)
            values.append(sum(pixels) / len(pixels))
            write_json(output / "report.json", report)
        return values

    try:
        folder = ROOT / "Assets/Scenes/PathTracingValidation/constant-environment"
        scene = json.loads((folder / "scene.json").read_text())
        preset = json.loads((folder / "render-preset.json").read_text())
        preset["pathTracing"].update(maxBounces=2, debugOutput=3, russianRouletteEnabled=False)
        for metallic in (() if args.rr_only else (0, 1)):
            for roughness in (.18, .4, .8):
                for material in scene["materials"]:
                    material.update(metallic=metallic, roughness=roughness)
                label = f"ggx-m{metallic}-r{roughness}"
                bsdf = run(scene, preset, label+"-bsdf", 1, [928,508,64,64])
                mis = run(scene, preset, label+"-mis", 5, [928,508,64,64])
                report["comparisons"].append(dict(case=label, **paired_agreement(bsdf, mis)))
                reference = plane_reference(scene, metallic, roughness)
                for method, values in (("bsdf", bsdf), ("mis", mis)):
                    report["comparisons"].append(dict(case=label+"-"+method+"-quadrature",
                        **reference_agreement(values, reference)))
        scene, preset = room_fixture()
        one = copy.deepcopy(preset)
        one["pathTracing"]["maxBounces"] = 1
        direct = run(scene, one, "room-b1", 0, [928,508,64,64])
        one["pathTracing"]["maxBounces"] = 2
        two = run(scene, one, "room-b2", 0, [928,508,64,64])
        off = run(scene, preset, "room-b8-rr-off", 0, [928,508,64,64])
        preset["pathTracing"]["russianRouletteEnabled"] = True
        on = run(scene, preset, "room-b8-rr-on", 0, [928,508,64,64])
        indirect = statistics.mean(off) - statistics.mean(direct)
        deep = statistics.mean(off) - statistics.mean(two)
        report["bounceControl"] = dict(oneBounceMean=statistics.mean(direct),
            twoBounceMean=statistics.mean(two), eightBounceMean=statistics.mean(off),
            indirectContribution=indirect, beyondTwoBounceContribution=deep, passed=indirect>1e-4 and deep>1e-4)
        report["comparisons"].append(dict(case="room-rr", **paired_agreement(off, on)))
        states = [r["status"] for r in report["comparisons"]]
        report["status"] = ("failed" if "failed" in states or not report["bounceControl"]["passed"] else
                            "inconclusive" if "inconclusive" in states else "passed")
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        write_json(output / "report.json", report)
    print(json.dumps(dict(status=report["status"], comparisons=report["comparisons"])))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
