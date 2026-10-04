"""Measure static editable fixtures against a common independent finite-sample reference."""
import argparse
from datetime import datetime, timezone
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess
import time
from types import SimpleNamespace

from compare_hdr import build_capture_command, capture, mean_image, read_pfm, rmse
from compare_convergence import metrics


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(value, indent=2) + "\n").replace("\n", "\r\n").encode())


def reference_metrics(images):
    average = mean_image(images)
    stats = metrics(images, average)
    return dict(seedCount=len(images), pairwiseDisagreementRmse=[rmse(a, b) for a, b in itertools.combinations(images, 2)],
        meanPixelStandardError=math.sqrt(stats["seedVariance"] / len(images)), meanRgb=stats["meanRgb"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, nargs="+", default=[8, 32, 128])
    parser.add_argument("--reference-samples", type=int, default=512)
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
    parser.add_argument("--reference-seeds", type=int, nargs="+", default=[101, 102])
    parser.add_argument("--modes", type=int, nargs="+", default=[1, 2, 5])
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--lighting-controls", action="store_true", help="Separate one/four-bounce local-light cohorts")
    args = parser.parse_args()
    if (len(set(args.seeds)) < 2 or len(set(args.reference_seeds)) < 2 or
            len(set(args.seeds)) != len(args.seeds) or len(set(args.reference_seeds)) != len(args.reference_seeds) or
            set(args.seeds) & set(args.reference_seeds) or min(args.samples) < 1 or
            args.samples != sorted(set(args.samples)) or args.reference_samples <= max(args.samples) or
            any(s < 0 or s > 0xffffffff for s in args.seeds + args.reference_seeds) or
            any(m not in (1, 2, 5) for m in args.modes)):
        parser.error("Use increasing samples, a higher reference, disjoint unique seeds, and constant modes 1/2/5")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Output must be empty: failed and completed runs must be retained")
    output.mkdir(parents=True, exist_ok=True)
    scenes = {
        "constant-environment": {"floor": [928, 508, 64, 64]},
        "roughness": {"roughness-0.18": [664, 424, 48, 48], "roughness-0.4": [936, 424, 48, 48],
            "roughness-0.8": [1208, 424, 48, 48]},
    }
    if args.lighting_controls:
        args.modes = [0]
        scenes = {"two-surface-direct": {"floor": [928, 508, 64, 64]},
            "two-surface-indirect": {"floor": [928, 508, 64, 64]}}
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(), workspace=str(root),
        commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        branch=subprocess.check_output(["git", "branch", "--show-current"], cwd=root, text=True).strip(),
        dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()),
        build="Debug x64", resolution=[1920, 1080], samples=args.samples, seeds=args.seeds,
        referenceSamples=args.reference_samples, referenceSeeds=args.reference_seeds, referenceMode=0 if args.lighting_controls else 5,
        modes=args.modes, domain="linear HDR RGB before ToneMap", smoke=args.smoke, captures=[], results=[],
        references=[], failures=[], status="running", scenes=scenes,
        lightingControls=args.lighting_controls,
        limitation="Finite-sample reference is not ground truth; standard error summarizes spatially averaged variance, not a confidence bound. Static camera; fixed bounce limit within each cohort. No sampler superiority or general GPU conclusions.")
    try:
        report["gpuDriver"] = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"], text=True, timeout=10).strip()
    except (OSError, subprocess.SubprocessError) as error:
        report["gpuDriverQueryError"] = str(error)
    write_json(output / "report.json", report)
    for scene_id, rois in scenes.items():
        folder = root / "Assets/Scenes/PathTracingValidation" / ("two-surface-indirect" if args.lighting_controls else scene_id)
        scene, preset = folder / "scene.json", folder / "render-preset.json"
        preset_data = json.loads(preset.read_text())
        if args.lighting_controls:
            preset_data["pathTracing"]["maxBounces"] = 1 if scene_id == "two-surface-direct" else 4
            preset = output / (scene_id + "-preset.json")
            write_json(preset, preset_data)
        settings = preset_data["pathTracing"]
        if settings["debugOutput"] != 3 or settings["russianRouletteEnabled"]:
            raise ValueError("Unexpected fixture settings")
        expected_scene_hash = hashlib.sha256(scene.read_bytes()).hexdigest()
        expected_preset_hash = hashlib.sha256(preset.read_bytes()).hexdigest()
        request = SimpleNamespace(root=root, exe=root / "bin/x64/Debug/RtPbrSurvey.exe", scene_file=scene,
            render_preset=preset, output=output, timeout=args.timeout, roi=list(rois.values())[0])
        def run(mode, seed, samples, name):
            name = scene_id + "-" + name
            command = build_capture_command(request, mode, seed, samples, output / (name + ".pfm"), output / (name + ".log"))
            start = time.monotonic()
            try:
                if (hashlib.sha256(scene.read_bytes()).hexdigest() != expected_scene_hash or
                        hashlib.sha256(preset.read_bytes()).hexdigest() != expected_preset_hash):
                    raise ValueError("Scene or preset changed during measurement")
                record, _ = capture(request, mode, seed, samples, name)
                if record["dimensions"] != (1920, 1080) or record["diagnostics"]["maxBounces"] != settings["maxBounces"]:
                    raise ValueError("Wrong resolution or bounce limit")
                if (hashlib.sha256(scene.read_bytes()).hexdigest() != expected_scene_hash or
                        hashlib.sha256(preset.read_bytes()).hexdigest() != expected_preset_hash):
                    raise ValueError("Scene or preset changed during capture")
                record.update(sceneId=scene_id, command=command, elapsedSeconds=time.monotonic()-start,
                    sceneHash=expected_scene_hash, presetHash=expected_preset_hash, settings=settings)
                report["captures"].append(record)
                pixels = {label: read_pfm(Path(record["path"]), roi)[1] for label, roi in rois.items()}
                return record, pixels
            except Exception as error:
                report["failures"].append(dict(name=name, command=command, error=str(error), elapsedSeconds=time.monotonic()-start))
                report["status"] = "failed"
                raise
            finally:
                write_json(output / "report.json", report)
        if args.smoke:
            run(report["referenceMode"], args.seeds[0], args.samples[0], "smoke")
            continue
        refs = {label: [] for label in rois}
        for seed in args.reference_seeds:
            _, pixels = run(report["referenceMode"], seed, args.reference_samples, f"reference-{seed}")
            for label in rois:
                refs[label].append(pixels[label])
        common = {label: mean_image(images) for label, images in refs.items()}
        for label in rois:
            report["references"].append(dict(sceneId=scene_id, roiName=label, roi=rois[label], **reference_metrics(refs[label])))
        for mode in args.modes:
            for samples in args.samples:
                images = {label: [] for label in rois}
                first = None
                for seed in args.seeds:
                    record, pixels = run(mode, seed, samples, f"mode-{mode}-spp-{samples}-seed-{seed}")
                    first = first or record
                    for label in rois:
                        images[label].append(pixels[label])
                if mode == args.modes[0] and samples == args.samples[0]:
                    repeat, _ = run(mode, args.seeds[0], samples, "repeat")
                    if first["sha256"] != repeat["sha256"]:
                        report["failures"].append(dict(sceneId=scene_id, error="Fixed-seed repeat differs"))
                for label in rois:
                    report["results"].append(dict(sceneId=scene_id, roiName=label, roi=rois[label], mode=mode,
                        samples=samples, **metrics(images[label], common[label])))
                write_json(output / "report.json", report)
    report["status"] = "failed" if report["failures"] else "complete"
    write_json(output / "report.json", report)
    lines = ["# Path Tracing convergence", "", report["limitation"], "", f"Commit: {report['commit']}; driver: {report.get('gpuDriver', 'unavailable')}", "",
        f"Evaluation seeds: {args.seeds}; reference: {args.reference_samples} spp, seeds {args.reference_seeds}, mode {report['referenceMode']}.", "",
        "| Scene / ROI | Mode | spp | RGB RMSE of mean | Seed variance | Mean RGB |", "|---|---:|---:|---:|---:|---|"]
    for result in report["results"]:
        lines.append(f"| {result['sceneId']} / {result['roiName']} | {result['mode']} | {result['samples']} | {result['meanImageRmse']:.6g} | {result['seedVariance']:.6g} | {result['meanRgb']} |")
    lines.extend(["", "## Reference uncertainty", "", "| Scene / ROI | Pairwise RGB RMSE | RMS pixel-channel standard error |", "|---|---|---:|"])
    for reference in report["references"]:
        lines.append(f"| {reference['sceneId']} / {reference['roiName']} | {reference['pairwiseDisagreementRmse']} | {reference['meanPixelStandardError']:.6g} |")
    (output / "report.md").write_bytes(("\n".join(lines)+"\n").replace("\n", "\r\n").encode())
    print(json.dumps(dict(status=report["status"], captures=len(report["captures"]), results=len(report["results"]))))
    return 0 if report["status"] == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
