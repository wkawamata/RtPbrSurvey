"""Validate GPU accumulation lifecycle with a serialized native HDR timeline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from validate_inputs import read_buffer


CASES = {
    "baseline-16": (16, 16, True, 1),
    "paused-16": (16, 16, True, 1),
    "resumed-32": (32, 32, True, 1),
    "reset-paused-0": (0, 0, False, 1),
    "reset-repeat-16": (16, 16, True, 1),
    "batch-4-32": (32, 32, True, 4),
    "non-accumulated-index-12": (3, 12, False, 3),
    "non-accumulated-repeat-12": (3, 12, False, 3),
    "fresh-32": (32, 32, True, 1),
    "camera-changed-16": (16, 16, True, 1),
    "camera-fresh-16": (16, 16, True, 1),
    "light-changed-16": (16, 16, True, 1),
    "light-fresh-16": (16, 16, True, 1),
    "material-changed-16": (16, 16, True, 1),
    "material-fresh-16": (16, 16, True, 1),
    "geometry-changed-16": (16, 16, True, 1),
    "geometry-fresh-16": (16, 16, True, 1),
    "resize-changed-16": (16, 16, True, 1),
    "resize-fresh-16": (16, 16, True, 1),
}
CHANGES = ("camera", "light", "material", "geometry", "resize")


def validate_records(records, completed):
    if completed != list(CASES) or [record.get("case") for record in records] != list(CASES):
        raise ValueError("Missing, duplicate or reordered GPU checkpoints")
    dimensions = None
    for record in records:
        count, index, valid, batch = CASES[record["case"]]
        if (record.get("accumulatedSamples") != count or record.get("frameSampleIndex") != index
                or record.get("historyValid") is not valid or record.get("paused") is not True
                or record.get("randomSeed") != 11 or record.get("samplesPerFrame") != batch):
            raise ValueError(f"Incorrect runtime state: {record['case']}")
        current = (record.get("renderWidth"), record.get("renderHeight"))
        if any(not isinstance(value, int) or value <= 0 for value in current):
            raise ValueError("Invalid render dimensions")
        resized = record["case"].startswith("resize-")
        if resized and current != (1280, 720):
            raise ValueError("Resize did not apply to render resources")
        if not resized and dimensions is not None and current != dimensions:
            raise ValueError("Render dimensions changed during lifecycle validation")
        if dimensions is None:
            dimensions = current
        if record["case"].endswith("-changed-16"):
            reason = dict(camera="Camera", light="Lighting", material="Material", geometry="Scene", resize="Render Size")
            change = record["case"].split("-")[0]
            expected = reason[change]
            allowed = ("Material", "Scene") if change == "material" else (expected,)
            entry = "Pending Resize" if change == "resize" else expected
            if record.get("resetReason") not in allowed or record.get("entryResetReason") != entry:
                raise ValueError("Incorrect changed-state reset reason")
        if record["case"].endswith("-fresh-16") and record.get("resetReason") != "Manual":
            raise ValueError("Fresh-history reference was not manually reset")
    return dimensions


def normalize_accumulation(raw, expected_count):
    if raw.ndim != 3 or raw.shape[2] != 4 or raw.size == 0 or not np.isfinite(raw).all():
        raise ValueError("Invalid raw accumulation buffer")
    if not np.all(raw[:, :, 3] == expected_count):
        raise ValueError("GPU per-pixel sample counts differ from CPU checkpoint")
    if expected_count == 0:
        if np.count_nonzero(raw) != 0:
            raise ValueError("Reset retained GPU radiance")
        return raw[:, :, :3].reshape(-1)
    return (raw[:, :, :3] / expected_count).reshape(-1)


def assess_images(images):
    if set(images) != set(CASES):
        raise ValueError("Missing HDR checkpoints")
    shapes = {image.shape for name, image in images.items() if not name.startswith("resize-")}
    if len(shapes) != 1 or next(iter(shapes)) == (0,):
        raise ValueError("Mismatched or empty HDR images")
    if any(not np.isfinite(image).all() for image in images.values()):
        raise ValueError("Non-finite HDR")
    if float(np.max(np.abs(images["baseline-16"]))) <= 1e-6:
        raise ValueError("Missing lit scene signal")
    comparisons = []
    for left, right in (("baseline-16", "paused-16"),
                        ("baseline-16", "reset-repeat-16"),
                        ("resumed-32", "fresh-32"),
                        ("non-accumulated-index-12", "non-accumulated-repeat-12")):
        exact = np.array_equal(images[left], images[right])
        comparisons.append(dict(left=left, right=right, exact=bool(exact)))
        if not exact:
            raise ValueError(f"History replay differs: {left} vs {right}")
    zero = float(np.max(np.abs(images["reset-paused-0"])))
    if zero != 0:
        raise ValueError("Reset while paused retained GPU radiance")
    if np.array_equal(images["baseline-16"], images["resumed-32"]):
        raise ValueError("Resume produced no new stochastic signal")
    a = images["batch-4-32"].astype(np.float64)
    b = images["fresh-32"].astype(np.float64)
    max_error = float(np.max(np.abs(a - b)))
    max_limit = 2e-6 * max(1.0, float(np.max(np.abs(b))))
    relative_rmse = float(np.sqrt(np.mean((a - b) ** 2)) / max(1e-12, np.sqrt(np.mean(b ** 2))))
    if max_error > max_limit or relative_rmse > 1e-6:
        raise ValueError("Sample batching differs beyond floating-point accumulation tolerance")
    changed_comparisons = []
    previous = "baseline-16"
    for change in CHANGES:
        changed = images[f"{change}-changed-16"]
        fresh = images[f"{change}-fresh-16"]
        if not np.array_equal(changed, fresh):
            raise ValueError(f"Old history contaminates {change} output")
        signal = None
        if change != "resize":
            signal = float(np.sqrt(np.mean((changed - images[previous]) ** 2)))
            if signal <= 1e-5:
                raise ValueError(f"No observable {change} mutation signal")
        changed_comparisons.append(dict(change=change, exact=True, mutationRmse=signal))
        previous = f"{change}-fresh-16"
    return dict(exactComparisons=comparisons, changedStateComparisons=changed_comparisons, resetMaxAbs=zero,
                batchMaxAbsError=max_error, batchMaxAbsLimit=max_limit,
                batchRelativeRmse=relative_rmse, batchRelativeRmseLimit=1e-6)


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=root / "build/Debug/RtPbrSurvey.exe")
    parser.add_argument("--scene", type=Path,
                        default=root / "Assets/Scene/PathTracingValidation/01-emissive-nee-baseline/scene.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Use a new or empty output directory")
    output.mkdir(parents=True, exist_ok=True)
    log = output / "runtime.log"
    command = [str(args.exe.resolve(strict=True)), "-SceneFile", str(args.scene.resolve(strict=True)),
               "-PathTracingHistoryValidation", str(output), "-LogToFile", str(log)]
    report = dict(schemaVersion=1, status="failed", command=command,
                  scope="GPU lifecycle and camera/light/material/instance-transform/resize history invalidation",
                  limitation="One static fixture on one GPU. Fresh history is a manual reset, not an independent renderer.")
    report["executableSha256"] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    report["sourceSha256"] = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                              for name in ("Engine/RtPbrSurveyEngine.cpp", "Renderer/ScreenshotCapture.cpp",
                                           "App/PathTracingHistoryValidation.cpp", "Tests/PathTracing/validate_history.py")}
    try:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        subprocess.run(command, cwd=root, check=True, timeout=args.timeout, startupinfo=startup)
        lines = log.read_text(encoding="utf-8-sig").splitlines()
        if any(line.startswith(("[ERROR]", "[CORRUPTION]", "[PathTracingHistoryFailure]")) for line in lines):
            raise RuntimeError("GPU/debug-layer failure in runtime log")
        if f"[PathTracingHistoryComplete] captures={len(CASES)}" not in lines:
            raise RuntimeError("Native timeline did not complete")
        records = [json.loads(line[len("[PathTracingHistory] "):]) for line in lines
                   if line.startswith("[PathTracingHistory] ")]
        completed = [line[len("[PathTracingHistoryCapture] "):] for line in lines
                     if line.startswith("[PathTracingHistoryCapture] ")]
        dimensions = validate_records(records, completed)
        report["records"] = records
        images = {}
        for record in records:
            path = Path(record["path"])
            meta, raw = read_buffer(path)
            if (meta["resource"] != "PathTracing.Accumulation" or meta["format"] != 2
                    or meta["sampleStartIndex"] != record["frameSampleIndex"]
                    or meta["randomSeed"] != record["randomSeed"]):
                raise ValueError("Native buffer metadata differs from checkpoint")
            size = (meta["width"], meta["height"])
            pixels = normalize_accumulation(raw, record["accumulatedSamples"])
            record["rawRgbaMaxAbs"] = float(np.max(np.abs(raw)))
            record["gpuSampleCount"] = float(raw[0, 0, 3])
            if size != (record["renderWidth"], record["renderHeight"]):
                raise ValueError("Capture dimensions differ from runtime state")
            images[record["case"]] = pixels
            record["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        report.update(status="passed", dimensions=dimensions, records=records,
                      assessment=assess_images(images),
                      warningCount=sum(line.startswith("[WARNING]") for line in lines),
                      errorCount=0, sceneSha256=hashlib.sha256(args.scene.read_bytes()).hexdigest(),
                      gpuDriver=subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version",
                                                         "--format=csv,noheader"], text=True).strip(),
                      commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
                      dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip()))
    except Exception as error:
        report["error"] = str(error)
        raise
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["assessment"], indent=2), flush=True)


if __name__ == "__main__":
    main()
