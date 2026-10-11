"""Capture linear HDR path tracing estimates and compare independent seeds."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


def read_pfm(path, roi):
    with open(path, "rb") as stream:
        if stream.readline().strip() != b"PF":
            raise ValueError("Expected RGB PFM")
        width, height = map(int, stream.readline().split())
        if float(stream.readline()) != -1.0:
            raise ValueError("Expected little-endian, unit-scale PFM")
        data = array("f")
        data.frombytes(stream.read())
    if sys.byteorder != "little":
        data.byteswap()
    if len(data) != width * height * 3 or not all(map(math.isfinite, data)):
        raise ValueError("Invalid dimensions or non-finite HDR data")
    x, y, w, h = roi
    if min(x, y) < 0 or min(w, h) <= 0 or x + w > width or y + h > height:
        raise ValueError("ROI is outside the image")
    result = array("f")
    for row in range(y, y + h):
        start = ((height - 1 - row) * width + x) * 3
        result.extend(data[start:start + w * 3])
    return (width, height), result


def mean_image(images):
    return [sum(values) / len(images) for values in zip(*images)]


def rmse(a, b):
    if len(a) != len(b) or not a:
        raise ValueError("Mismatched images")
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))


def write_direct_only_preset(source, destination):
    preset = json.loads(source.read_text(encoding="utf-8-sig"))
    lighting = preset["lighting"]
    lighting.update(skyboxEnabled=False, diffuseIblEnabled=False,
                    specularIblEnabled=False, emissiveEnabled=False)
    preset.setdefault("shadow", {})["enabled"] = True
    preset.setdefault("pathTracing", {}).update(
        maxBounces=1, directLightingEnabled=True, environmentEnabled=False,
        emissiveEnabled=False, russianRouletteEnabled=False)
    destination.write_text(json.dumps(preset, indent=2) + "\n", encoding="utf-8")


def build_capture_command(args, mode, seed, samples, path, log):
    command = [str(args.exe)]
    if args.scene_file:
        command.extend(["-SceneFile", str(args.scene_file)])
        if args.render_preset:
            command.extend(["-RenderPreset", str(args.render_preset)])
    else:
        command.extend(["-AutoSelectGltfAsset", args.scene, "-UseSceneDefaults"])
    command.extend(["-EnablePathTracing", "-PathTracingSamples", str(samples),
                    "-PathTracingSeed", str(seed), "-PathTracingEnvironmentMode", str(mode),
                    "-CapturePath", str(path), "-LogToFile", str(log), "-ExitAfterCapture"])
    return command


def capture(args, mode, seed, samples, name):
    path = args.output / (name + ".pfm")
    log = args.output / (name + ".log")
    path.unlink(missing_ok=True)
    log.unlink(missing_ok=True)
    command = build_capture_command(args, mode, seed, samples, path, log)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    subprocess.run(command, cwd=args.root, check=True, timeout=args.timeout, startupinfo=startup)
    lines = log.read_text(encoding="utf-8-sig").splitlines()
    if any(line.startswith(("[ERROR]", "[CORRUPTION]")) for line in lines):
        raise RuntimeError(f"D3D12 error in {log}")
    diagnostics = [json.loads(line[len("[PathTracing] "):]) for line in lines
                   if line.startswith("[PathTracing] ")][-1]
    if (diagnostics["accumulatedSamples"] != samples or diagnostics["randomSeed"] != seed
            or diagnostics["environmentSamplingMode"] != mode):
        raise RuntimeError("Capture settings differ from request")
    dimensions, pixels = read_pfm(path, args.roi)
    record = dict(path=str(path), mode=mode, seed=seed, samples=samples, dimensions=dimensions,
                  sha256=hashlib.sha256(path.read_bytes()).hexdigest(), diagnostics=diagnostics,
                  warningCount=sum(line.startswith("[WARNING]") for line in lines))
    print(f"Captured {name}", flush=True)
    return record, pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--exe", type=Path, default=root / "bin/x64/Debug/RtPbrSurvey.exe")
    parser.add_argument("--output", type=Path, default=root / "bin/PathTracing-HdrComparison")
    parser.add_argument("--scene", default="DamagedHelmet")
    parser.add_argument("--scene-file", type=Path)
    parser.add_argument("--render-preset", type=Path)
    parser.add_argument("--direct-only", action="store_true")
    parser.add_argument("--roi", type=int, nargs=4, default=[885, 460, 175, 180])
    parser.add_argument("--samples", type=int, default=64)
    parser.add_argument("--reference-samples", type=int, default=1024)
    parser.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
    parser.add_argument("--reference-seeds", type=int, nargs="+", default=[101, 102])
    parser.add_argument("--modes", type=int, nargs="+", default=[0, 3, 4, 6, 7])
    parser.add_argument("--reference-mode", type=int, default=7)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--require-seed-variation", action="store_true")
    args = parser.parse_args()
    if (len(set(args.seeds)) < 2 or len(set(args.reference_seeds)) < 2
            or len(set(args.seeds)) != len(args.seeds) or len(set(args.reference_seeds)) != len(args.reference_seeds)
            or any(s < 0 or s > 0xffffffff for s in args.seeds + args.reference_seeds)
            or set(args.seeds) & set(args.reference_seeds)
            or min(args.samples, args.reference_samples) <= 0
            or args.reference_samples <= args.samples
            or not args.modes or any(m not in range(8) for m in args.modes)
            or args.reference_mode not in range(8)):
        parser.error("Use at least two distinct seeds per group, disjoint reference seeds, more reference samples, and modes 0-7")
    if args.render_preset and not args.scene_file:
        parser.error("--render-preset requires --scene-file")
    if args.direct_only and not args.render_preset:
        parser.error("--direct-only requires --render-preset")
    if args.scene_file:
        args.scene_file = args.scene_file.resolve(strict=True)
    if args.render_preset:
        args.render_preset = args.render_preset.resolve(strict=True)
    args.output = args.output.resolve()
    args.exe = args.exe.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    source_preset = args.render_preset
    if args.direct_only:
        args.render_preset = args.output / "direct-only-preset.json"
        write_direct_only_preset(source_preset, args.render_preset)
    for report_name in ("report.json", "report.md"):
        (args.output / report_name).unlink(missing_ok=True)
    records, references = [], []
    for seed in args.reference_seeds:
        record, pixels = capture(args, args.reference_mode, seed, args.reference_samples, f"reference-{seed}")
        if records and record["dimensions"] != records[0]["dimensions"]:
            raise ValueError("Reference dimensions changed")
        records.append(record)
        references.append(pixels)
    reference = mean_image(references)
    results = []
    for mode in args.modes:
        images = []
        for seed in args.seeds:
            record, pixels = capture(args, mode, seed, args.samples, f"mode-{mode}-seed-{seed}")
            if record["dimensions"] != records[0]["dimensions"]:
                raise ValueError("Render dimensions changed")
            records.append(record)
            images.append(pixels)
        if mode == args.modes[0]:
            repeat, _ = capture(args, mode, args.seeds[0], args.samples, "repeat")
            original = records[-len(args.seeds)]
            if repeat["sha256"] != original["sha256"]:
                raise RuntimeError("Fixed-seed HDR capture is not deterministic")
            records.append(repeat)
        average = mean_image(images)
        variance = sum(sum((image[i] - average[i]) ** 2 for image in images) /
                       (len(images) - 1) for i in range(len(average))) / len(average)
        results.append(dict(mode=mode, perSeedRmse=[rmse(image, reference) for image in images],
                            meanImageRmse=rmse(average, reference), seedVariance=variance,
                            meanRadiance=sum(average) / len(average)))
    if args.require_seed_variation and max(result["seedVariance"] for result in results) < 1e-12:
        raise RuntimeError("Expected stochastic seed variation was absent")
    report = dict(schemaVersion=1, domain="linear-hdr-rgb",
                  scene=str(args.scene_file) if args.scene_file else args.scene,
                  sceneFileSha256=hashlib.sha256(args.scene_file.read_bytes()).hexdigest() if args.scene_file else None,
                  sourceRenderPreset=str(source_preset) if args.direct_only else None,
                  sourceRenderPresetSha256=hashlib.sha256(source_preset.read_bytes()).hexdigest() if args.direct_only else None,
                  renderPreset=str(args.render_preset) if args.render_preset else None,
                  renderPresetSha256=hashlib.sha256(args.render_preset.read_bytes()).hexdigest() if args.render_preset else None,
                  directOnly=args.direct_only,
                  roi=args.roi,
                  samples=args.samples, referenceSamples=args.reference_samples,
                  referenceMode=args.reference_mode, referenceSeeds=args.reference_seeds, seeds=args.seeds,
                  referenceDisagreementRmse=rmse(references[0], references[1]),
                  limitation="Finite-sample reference, not ground truth. Two-seed reference disagreement is an uncertainty indicator, not a confidence bound.",
                  deterministic=True, results=results, captures=records)
    report["commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.root, text=True).strip()
    report["dirty"] = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=args.root, text=True).strip())
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["# Path Tracing HDR Comparison", "", report["limitation"], "",
             f"ROI: {args.roi}; samples: {args.samples}; reference: {args.reference_samples} x {len(references)}",
             f"Reference disagreement RMSE: {report['referenceDisagreementRmse']:.8g}", "",
             "| Mode | Mean-image RMSE | Seed variance | Mean RGB radiance |",
             "|---|---:|---:|---:|"]
    for result in results:
        lines.append(f"| {result['mode']} | {result['meanImageRmse']:.8g} | {result['seedVariance']:.8g} | {result['meanRadiance']:.8g} |")
    (args.output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines), flush=True)


if __name__ == "__main__":
    main()
