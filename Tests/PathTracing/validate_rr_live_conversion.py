"""Validate opt-in live PT-to-RR conversion and capture-time admission stamps."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_denoiser_distances import capture
from validate_history import CASES, CHANGES, validate_records
from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_rr_texture_conversion import RESOURCES, reference

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ("RrColor", "RrDiffuseAlbedo", "RrSpecularAlbedo", "RrNormal", "RrRoughness",
           "RrLinearDepth", "RrMotion", "RrHitDistance", "RrStatus")


def assess_numeric(meta, values):
    summary = meta["rrNumericFrame"]
    counts = np.bincount(values.astype(np.int64).ravel(), minlength=7).tolist()
    if not np.isfinite(values).all() or not np.array_equal(values, np.floor(values)) or len(counts) != 7:
        raise ValueError("Invalid runtime status code")
    if summary["statusCounts"] != counts or summary["unknownPixels"] != 0:
        raise ValueError("Readback summary differs from captured pixels")
    if summary["nativeEvaluationQualified"] or meta["rrConversion"]["nativeEvaluationQualified"]:
        raise ValueError("Numeric diagnostics incorrectly qualified native RR")
    if summary["completed"] != meta["rrConversion"]["recorded"]:
        raise ValueError("Capture completion stamp mismatch")
    expected = summary["completed"] and meta["rrConversion"]["hostGate"] == 0 and counts[0] == values.size
    if summary["passed"] != expected:
        raise ValueError("Whole-frame numeric gate mismatch")
    if summary["frameId"] != meta["rrConversion"]["frameId"]:
        raise ValueError("GPU result was relabelled with a newer production frame")
    return counts


def history(exe, scene, preset, folder):
    folder.mkdir()
    log = folder/"runtime.log"
    command = [str(exe), "-SceneFile", str(scene), "-RenderPreset", str(preset),
               "-PathTracingHistoryValidation", str(folder), "-LogToFile", str(log),
               "-DebugPreviewResource", "PathTracing.RrStatus"]
    subprocess.run(command, cwd=ROOT, timeout=600, check=True)
    lines = log.read_text(encoding="utf-8-sig").splitlines()
    if any(line.startswith(("[ERROR]", "[CORRUPTION]", "[PathTracingHistoryFailure]")) for line in lines):
        raise ValueError("History runtime error")
    records = [json.loads(line[len("[PathTracingHistory] "):]) for line in lines
               if line.startswith("[PathTracingHistory] ")]
    completed = [line[len("[PathTracingHistoryCapture] "):] for line in lines
                 if line.startswith("[PathTracingHistoryCapture] ")]
    validate_records(records, completed)
    if lines.count("[PathTracingHistoryComplete] captures="+str(len(CASES))) != 1:
        raise ValueError("Incomplete history capture")
    images, summaries = {}, []
    for record in records:
        path = Path(record["path"])
        meta, values = read_buffer(path)
        if meta["resource"] != "PathTracing.RrStatus" or meta["format"] != 41:
            raise ValueError("Wrong lifecycle resource")
        if (meta["width"], meta["height"]) != (record["renderWidth"], record["renderHeight"]):
            raise ValueError("History resize did not update RR resources")
        counts = assess_numeric(meta, values)
        if meta["rrConversion"]["recorded"] or meta["rrNumericFrame"]["passed"]:
            raise ValueError("Paused capture was relabelled as newly converted")
        if record["case"] in ("reset-paused-0", "batch-4-32", "non-accumulated-index-12", "non-accumulated-repeat-12"):
            if (values != 6).any():
                raise ValueError("Reset/incompatible profile retained candidate status")
        images[record["case"]] = values
        summaries.append(dict(case=record["case"], counts=counts, rrConversion=meta["rrConversion"],
                              rrNumericFrame=meta["rrNumericFrame"], sha256=sha(path)))
    pairs = [("baseline-16", "paused-16"), ("baseline-16", "reset-repeat-16"), ("resumed-32", "fresh-32")]
    pairs += [(name+"-changed-16", name+"-fresh-16") for name in CHANGES]
    for left, right in pairs:
        if not np.array_equal(images[left], images[right]):
            raise ValueError("Lifecycle status replay mismatch: "+left+" / "+right)
    return dict(command=command, records=summaries, exactComparisons=pairs, d3d12Errors=0, logSha256=sha(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--history", action="store_true")
    args = parser.parse_args()
    source, output, exe = args.source.resolve(strict=True), args.output.resolve(), args.exe.resolve(strict=True)
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error("Output must be a separate empty directory")
    scene = json.loads((source/"scene.json").read_text())
    preset = json.loads((source/"preset.json").read_text())
    preset["pathTracing"].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True,
                                 denoiserDistanceDiagnostics=True, reconstructionDiagnostics=True)
    write_json(output/"scene.json", scene)
    write_json(output/"preset.json", preset)
    (output/"validation-script.py").write_bytes(Path(__file__).read_bytes())
    report = dict(status="running", nativeEvaluationQualified=False, captures=[], failures=[],
                  exeSha256=sha(exe), producerShaderSha256=sha(exe.parent/"shaders_PathTracing_CSMain.cso"),
                  conversionShaderSha256=sha(exe.parent/"shaders_PathTracingRrConversion_CSMain.cso"),
                  scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/"scene.json"),
                  presetSha256=sha(output/"preset.json"))
    try:
        inputs, observed, common = [], [], None
        # Status first gives a fast admission/provenance smoke check.
        names = ("RrStatus",)+RESOURCES+OUTPUTS[:-1]
        for name in names:
            (meta, values), record = capture(exe, output, name, output/"preset.json", name)
            if meta["resource"] != "PathTracing."+name:
                raise ValueError("Wrong capture resource")
            stamp = meta["rrConversion"]
            if not stamp["recorded"] or stamp["hostGate"] != 0 or stamp["sampleStartIndex"] != meta["sampleStartIndex"]:
                raise ValueError("Missing or mismatched current production stamp")
            comparable = {k: v for k, v in meta.items() if k not in ("resource", "format", "rrNumericFrame")}
            if common is not None and comparable != common:
                raise ValueError("Live capture cohort mismatch")
            common = comparable
            if name == "RrStatus":
                status = values
                report["numericFrame"] = meta["rrNumericFrame"]
                report["statusCounts"] = assess_numeric(meta, values)
            elif name in RESOURCES:
                inputs.append(values)
            else:
                slot = OUTPUTS.index(name)
                expected_format = 2 if slot < 4 else 16 if slot == 6 else 41
                if meta["format"] != expected_format:
                    raise ValueError("Wrong derived texture format")
                observed.append(values)
            report["captures"].append(record)
            write_json(output/"report.json", report)
        observed.append(status)
        expected = reference(inputs, np.asarray(common["inverseViewProjection"]).T)
        report["maximumErrors"] = []
        for slot, (actual, target) in enumerate(zip(observed, expected)):
            if actual.shape != target.shape or not np.allclose(actual, target, atol=5e-5, rtol=5e-5):
                raise ValueError("Live/reference mismatch: "+OUTPUTS[slot])
            report["maximumErrors"].append(float(np.max(np.abs(actual-target))))
        if report["statusCounts"][0] < 100 or not any(report["statusCounts"][1:]):
            raise ValueError("Fixture lacks candidates or excluded pixels")
        for name, settings in (("batch", dict(samplesPerFrame=4)), ("distance-off", dict(denoiserDistanceDiagnostics=False))):
            excluded = json.loads(json.dumps(preset))
            excluded["pathTracing"].update(settings)
            path = output/(name+".json")
            write_json(path, excluded)
            (meta, values), record = capture(exe, output, "RrStatus", path, name+"-status")
            report["captures"].append(record)
            assess_numeric(meta, values)
            if meta["rrConversion"]["hostGate"] != 6 or (values != 6).any():
                raise ValueError("Incompatible profile passed host gate")
            if name == "batch":
                (_, color), record = capture(exe, output, "RrColor", path, "batch-color")
                report["captures"].append(record)
                if (color != 0).any():
                    raise ValueError("Rejected profile retained RR color")
            write_json(output/"report.json", report)
        if args.history:
            report["history"] = history(exe, output/"scene.json", output/"preset.json", output/"history")
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", failures=[str(error)])
    report["limitation"] = "Live conversion diagnostics only; no RR SDK evaluation or distance-policy qualification."
    write_json(output/"report.json", report)
    print(report["status"], report.get("statusCounts"), report.get("maximumErrors"), report["failures"])
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
