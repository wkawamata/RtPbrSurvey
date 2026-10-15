"""Aggregate fail-closed NRD contract, conversion, reconstruction, and GUI-less evidence."""

import argparse
import json
import re
from pathlib import Path

PASS_STATUSES = {"pass", "passed", "ok", "true"}
CONVERSION_VARIANTS = {"hit-only", "hypothetical-environment"}
RECONSTRUCTION_MODES = {"3x3", "5x5"}
HOST_ONLY_CATEGORIES = {"snapshot-token", "scene-range-proof", "trace-provenance"}
PRODUCTION_RUNTIME_REASON = (
    "Full NRD RELAX denoising, history and remodulated composition are not integrated; "
    "experimental conversion diagnostics do not enable the backend."
)


def load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _status_ok(value):
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in PASS_STATUSES
    return False


def _is_mapping(value):
    return isinstance(value, dict)


def _nonempty_string(value):
    return isinstance(value, str) and value.strip() != ""


def _record_errors(record):
    for key in ("d3d12Errors", "d3d12ErrorCount", "errorCount"):
        if key in record:
            return int(record[key]) != 0
    return False


def _unique_key_violations(records, key_fields):
    seen = set()
    violations = 0
    for record in records:
        key = tuple(record.get(field) for field in key_fields)
        if key in seen:
            violations += 1
        seen.add(key)
    return violations, seen


def validate_contract_report(report):
    failures = []
    if not _is_mapping(report):
        return ["contract report is not a JSON object"], {}
    if not _status_ok(report.get("status")):
        failures.append("contract report status is not pass/passed")
    if report.get("schemaVersion") != 2 or report.get("failures") != []:
        failures.append("contract report has an incomplete schema or failures")
    for key in ("corpusSha256", "cpuExeSha256", "gpuExeSha256", "gpuShaderSha256", "scriptSha256",
                "gpuHelperSha256", "cpuStdoutSha256"):
        if not isinstance(report.get(key), str) or not re.fullmatch('[0-9a-f]{64}', report[key]):
            failures.append("contract report is missing execution provenance: "+key)
    if report.get("inputReady") is not False:
        failures.append("contract report inputReady is not false")
    if report.get("runtimeGeometrySnapshotVerified") is not False:
        failures.append("contract report runtimeGeometrySnapshotVerified is not false")
    if report.get("gpuProofClaimed") is True or report.get("gpuProof") is True:
        failures.append("contract report claims GPU proof for host-only checks")
    host_only = set(report.get("hostOnlyCategories", report.get("hostOnly", [])))
    if not HOST_ONLY_CATEGORIES.issubset(host_only):
        failures.append("contract report is missing required host-only categories")
    gpu_representable = report.get("gpuRepresentableHostChecks")
    if gpu_representable is True:
        failures.append("contract report claims host checks are GPU representable")
    records = report.get("records", [])
    if records is None:
        records = []
    if not isinstance(records, list):
        failures.append("contract report records is not a list")
        records = []
    if records:
        valid_records = [r for r in records if isinstance(r, dict)]
        violations, _ = _unique_key_violations(valid_records, ("id", "caseId"))
        if violations:
            failures.append("contract report contains duplicate case IDs")
        for record in valid_records:
            if type(record.get('d3d12Errors')) is not int or record['d3d12Errors'] != 0:
                failures.append("contract report contains D3D12 errors")
    cpu_ids = {"cpu-pixel-convertible-base", "cpu-pixel-stale-snapshot", "cpu-pixel-trace-required",
               "cpu-pixel-reconstruction-required", "cpu-pixel-exclude-primary-miss"}
    expected = {('cpu', case) for case in cpu_ids} | {
        ('gpu', 'cpu-pixel-convertible-base'), ('gpu', 'cpu-pixel-exclude-primary-miss')}
    if (len(records) != len(expected) or any(not isinstance(r, dict) or r.get('passed') is not True or
            type(r.get('d3d12Errors')) is not int or r['d3d12Errors'] != 0 or
            r.get('id') != str(r.get('domain'))+':'+str(r.get('caseId')) for r in records) or
            {(r.get('domain'), r.get('caseId')) for r in records if isinstance(r, dict)} != expected):
        failures.append("contract report is missing exact executed case coverage")
    summary = {
        "status": "passed" if not failures else "failed",
        "scope": "cpu-only",
        "hostOnly": True,
        "gpuProof": False,
        "hostOnlyCategories": sorted(host_only),
        "recordCount": len(records),
    }
    return failures, summary


def validate_conversion_report(report):
    failures = []
    if not _is_mapping(report):
        return ["conversion report is not a JSON object"], {}
    if not _status_ok(report.get("status")):
        failures.append("conversion report status is not pass/passed")
    if report.get("inputReady") is not False:
        failures.append("conversion report inputReady is not false")
    if report.get("runtimeGeometrySnapshotVerified") is not False:
        failures.append("conversion report runtimeGeometrySnapshotVerified is not false")
    for key in ("sourceReportSha256", "exeSha256", "shaderSha256", "scriptSha256"):
        if key in report and not _nonempty_string(report.get(key)):
            failures.append("conversion report has an empty " + key)
    records = report.get("records")
    if not isinstance(records, list):
        failures.append("conversion report records must be a list")
        records = []
    if len(records) != len(CONVERSION_VARIANTS):
        failures.append("conversion report expected %d variant records" % len(CONVERSION_VARIANTS))
    for key in ("variant",):
        pass
    variants = []
    seen_variants = set()
    duplicates = 0
    for record in records:
        if not _is_mapping(record):
            failures.append("conversion report contains a non-object record")
            continue
        variant = record.get("variant")
        variants.append(variant)
        if variant in seen_variants:
            duplicates += 1
        seen_variants.add(variant)
        if not _nonempty_string(record.get("inputSha256")) or not _nonempty_string(record.get("outputSha256")):
            failures.append("conversion record is missing raw input/output hash: " + str(variant))
        if _record_errors(record):
            failures.append("conversion record contains D3D12 errors: " + str(variant))
        expected_hypothetical = variant == "hypothetical-environment"
        if "hypotheticalEnvironment" in record and record.get("hypotheticalEnvironment") is not expected_hypothetical:
            failures.append("conversion record has contradictory hypothetical-environment flag: " + str(variant))
        assessment = record.get("assessment")
        if _is_mapping(assessment):
            if assessment.get("inputReady") is not False:
                failures.append("conversion assessment inputReady is not false: " + str(variant))
            if "rejectedPixels" in assessment and not isinstance(assessment.get("rejectedPixels"), int):
                failures.append("conversion assessment rejectedPixels is not an integer: " + str(variant))
    if duplicates:
        failures.append("conversion report contains duplicate variants")
    if set(variants) != CONVERSION_VARIANTS:
        failures.append("conversion report expected variants %s" % sorted(CONVERSION_VARIANTS))
    summary = {
        "status": "passed" if not failures else "failed",
        "scope": "gpu-arithmetic",
        "variantCount": len(records),
        "variants": sorted(set(variants)),
        "d3d12Errors": sum(int(r.get("d3d12Errors", 0)) for r in records if _is_mapping(r)),
    }
    return failures, summary


def validate_reconstruction_report(report, source_report=None):
    failures = []
    if not _is_mapping(report):
        return ["reconstruction report is not a JSON object"], {}
    if not _status_ok(report.get("status")):
        failures.append("reconstruction report status is not pass/passed")
    if report.get("inputReady") is not False:
        failures.append("reconstruction report inputReady is not false")
    if report.get("runtimeGeometrySnapshotVerified") is not False:
        failures.append("reconstruction report runtimeGeometrySnapshotVerified is not false")
    records = report.get("records")
    if not isinstance(records, list):
        failures.append("reconstruction report records must be a list")
        records = []
    expected_count = len(CONVERSION_VARIANTS) * len(RECONSTRUCTION_MODES)
    if len(records) != expected_count:
        failures.append("reconstruction report expected %d records" % expected_count)
    keys = set()
    duplicates = 0
    variants = set()
    modes = set()
    for record in records:
        if not _is_mapping(record):
            failures.append("reconstruction report contains a non-object record")
            continue
        variant = record.get("variant")
        mode = record.get("mode")
        pair = (variant, mode)
        if pair in keys:
            duplicates += 1
        keys.add(pair)
        variants.add(variant)
        modes.add(mode)
        if variant not in CONVERSION_VARIANTS:
            failures.append("reconstruction report has an unknown variant: " + str(variant))
        if mode not in RECONSTRUCTION_MODES:
            failures.append("reconstruction report has an unknown mode: " + str(mode))
        if not _nonempty_string(record.get("shaderSha256")):
            failures.append("reconstruction record is missing shader hash: " + str(pair))
        if _record_errors(record):
            failures.append("reconstruction record contains D3D12 errors: " + str(pair))
        if "hypotheticalEnvironment" in record and record.get("hypotheticalEnvironment") is not (variant == "hypothetical-environment"):
            failures.append("reconstruction record has contradictory hypothetical flag: " + str(pair))
    if duplicates:
        failures.append("reconstruction report contains duplicate variant/mode records")
    if variants != CONVERSION_VARIANTS:
        failures.append("reconstruction report expected variants %s" % sorted(CONVERSION_VARIANTS))
    if modes != RECONSTRUCTION_MODES:
        failures.append("reconstruction report expected modes %s" % sorted(RECONSTRUCTION_MODES))
    if source_report is not None and _is_mapping(source_report):
        if ("sourceReportSha256" in report and "sourceReportSha256" in source_report and
                report.get("sourceReportSha256") != source_report.get("sourceReportSha256")):
            failures.append("reconstruction source/report hash does not match conversion source report")
    for key in ("sourceReportSha256", "exeSha256", "scriptSha256"):
        if key in report and not _nonempty_string(report.get(key)):
            failures.append("reconstruction report has an empty " + key)
    summary = {
        "status": "passed" if not failures else "failed",
        "scope": "sdk-reconstruction",
        "recordCount": len(records),
        "variants": sorted(variants),
        "modes": sorted(modes),
        "successCount": len(records),
    }
    return failures, summary


def validate_converted_reconstruction_report(report, conversion_report=None, reconstruction_report=None):
    failures = []
    if not _is_mapping(report):
        return ["converted reconstruction report is not a JSON object"], {}
    if not _status_ok(report.get("status")):
        failures.append("converted reconstruction report status is not pass/passed")
    if report.get("inputReady") is not False:
        failures.append("converted reconstruction report inputReady is not false")
    if report.get("runtimeGeometrySnapshotVerified") is not False:
        failures.append("converted reconstruction report runtimeGeometrySnapshotVerified is not false")
    records = report.get("records")
    if not isinstance(records, list):
        failures.append("converted reconstruction report records must be a list")
        records = []
    expected_count = len(CONVERSION_VARIANTS) * len(RECONSTRUCTION_MODES)
    if len(records) != expected_count:
        failures.append("converted reconstruction report expected %d records" % expected_count)
    keys = set()
    duplicates = 0
    variants = set()
    modes = set()
    physical_errors = []
    for record in records:
        if not _is_mapping(record):
            failures.append("converted reconstruction report contains a non-object record")
            continue
        variant = record.get("variant")
        mode = record.get("mode")
        pair = (variant, mode)
        if pair in keys:
            duplicates += 1
        keys.add(pair)
        variants.add(variant)
        modes.add(mode)
        if variant not in CONVERSION_VARIANTS:
            failures.append("converted reconstruction report has an unknown variant: " + str(variant))
        if mode not in RECONSTRUCTION_MODES:
            failures.append("converted reconstruction report has an unknown mode: " + str(mode))
        if _record_errors(record):
            failures.append("converted reconstruction record contains D3D12 errors: " + str(pair))
        if "hypotheticalEnvironment" in record and record.get("hypotheticalEnvironment") is not (variant == "hypothetical-environment"):
            failures.append("converted reconstruction record has contradictory hypothetical flag: " + str(pair))
        lobes = record.get("lobes")
        if not _is_mapping(lobes) or set(lobes) != {"Diffuse", "Specular"}:
            failures.append("converted reconstruction record is missing lobe results: " + str(pair))
            continue
        for lobe_name, lobe in lobes.items():
            if not _is_mapping(lobe):
                failures.append("invalid lobe result: " + str(pair))
                continue
            error = lobe.get("maximumPhysicalRoundTripError")
            if isinstance(error, (int, float)):
                physical_errors.append(float(error))
                if float(error) > 4e-5:
                    failures.append("converted reconstruction RGB round trip exceeds tolerance: " + str(pair))
    if duplicates:
        failures.append("converted reconstruction report contains duplicate variant/mode records")
    if variants != CONVERSION_VARIANTS:
        failures.append("converted reconstruction report expected variants %s" % sorted(CONVERSION_VARIANTS))
    if modes != RECONSTRUCTION_MODES:
        failures.append("converted reconstruction report expected modes %s" % sorted(RECONSTRUCTION_MODES))
    if len(keys) != expected_count:
        failures.append("converted reconstruction report is missing expected case coverage")
    if _is_mapping(conversion_report) and report.get("sourceReportSha256") != conversion_report.get("sourceReportSha256"):
        failures.append("converted reconstruction source hash does not match conversion report")
    for key in ("sourceReportSha256", "conversionReportSha256"):
        if key in report and not _nonempty_string(report.get(key)):
            failures.append("converted reconstruction report has an empty " + key)
    hyp_records = [r for r in records if r.get("variant") == "hypothetical-environment"]
    hyp_passed = all(_is_mapping(r) for r in hyp_records) and len(hyp_records) == len(RECONSTRUCTION_MODES)
    summary = {
        "status": "passed" if not failures else "failed",
        "scope": "sdk-reconstruction-with-captured-conversion",
        "recordCount": len(records),
        "physicalMaximumRoundTripError": max(physical_errors) if physical_errors else None,
        "hypotheticalEnvironmentRecords": len(hyp_records),
    }
    return failures, summary


def aggregate_reports(reports):
    reports = reports or {}
    failures = []
    summaries = {}

    contract_report = reports.get("contract")
    conversion_report = reports.get("conversion")
    reconstruction_report = reports.get("reconstruction")
    converted_report = reports.get("convertedReconstruction")

    # Required input validation.
    required = {
        "contract": contract_report,
        "conversion": conversion_report,
        "reconstruction": reconstruction_report,
        "convertedReconstruction": converted_report,
    }
    for name, report in required.items():
        if report is None:
            failures.append("missing required report: " + name)

    contract_failures, contract_summary = validate_contract_report(contract_report) if contract_report is not None else ([], {})
    conversion_failures, conversion_summary = validate_conversion_report(conversion_report) if conversion_report is not None else ([], {})
    reconstruction_failures, reconstruction_summary = validate_reconstruction_report(
        reconstruction_report, conversion_report
    ) if reconstruction_report is not None else ([], {})
    converted_failures, converted_summary = validate_converted_reconstruction_report(
        converted_report, conversion_report, reconstruction_report
    ) if converted_report is not None else ([], {})

    failures.extend("contract: " + item for item in contract_failures)
    failures.extend("conversion: " + item for item in conversion_failures)
    failures.extend("reconstruction: " + item for item in reconstruction_failures)
    failures.extend("convertedReconstruction: " + item for item in converted_failures)

    # Cross-report stale evidence checks.
    if _is_mapping(conversion_report) and _is_mapping(converted_report):
        if conversion_report.get("sourceReportSha256") != converted_report.get("sourceReportSha256"):
            failures.append("conversion and converted-reconstruction source report hashes disagree")
        if conversion_report.get("scriptSha256") and converted_report.get("scriptSha256") == conversion_report.get("scriptSha256"):
            # Converted reconstruction may legitimately use a different script; only flag identical report content mistaken as same.
            pass

    # CPU-only claim: host-only provenance only.
    summaries["cpuOnly"] = contract_summary if contract_summary else {
        "status": "unavailable",
        "scope": "cpu-only",
        "hostOnly": True,
        "gpuProof": False,
    }
    summaries["gpuArithmetic"] = conversion_summary if conversion_summary else {
        "status": "unavailable",
        "scope": "gpu-arithmetic",
    }
    summaries["sdkReconstruction"] = reconstruction_summary if reconstruction_summary else {
        "status": "unavailable",
        "scope": "sdk-reconstruction",
    }
    summaries["hypotheticalEnvironment"] = {
        "status": "passed" if (
            _is_mapping(conversion_report)
            and _is_mapping(converted_report)
            and not any(f.startswith(("conversion:", "convertedReconstruction:")) for f in failures)
        ) else "failed",
        "scope": "hypothetical-environment",
        "hypothetical": True,
        "runtimeRangeProof": False,
        "productionGeometry": False,
    }
    summaries["productionRuntime"] = {
        "available": False,
        "integrated": False,
        "status": "unavailable-not-integrated",
        "reason": PRODUCTION_RUNTIME_REASON,
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
    }

    aggregate_status = "passed" if not failures else "failed"
    return {
        "schemaVersion": 1,
        "kind": "nrd-validation-aggregate",
        "status": aggregate_status,
        "failures": failures,
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
        "statuses": summaries,
        "cpuOnlyStatus": summaries["cpuOnly"].get("status", "unknown"),
        "gpuArithmeticStatus": summaries["gpuArithmetic"].get("status", "unknown"),
        "sdkReconstructionStatus": summaries["sdkReconstruction"].get("status", "unknown"),
        "hypotheticalEnvironmentStatus": summaries["hypotheticalEnvironment"].get("status", "unknown"),
        "productionRuntimeStatus": "unavailable-not-integrated",
        "hostOnlyCategories": sorted(HOST_ONLY_CATEGORIES),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract-report", type=Path)
    parser.add_argument("--conversion-report", type=Path)
    parser.add_argument("--reconstruction-report", type=Path)
    parser.add_argument("--converted-reconstruction-report", type=Path)
    parser.add_argument("--input-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    reports = {}
    contract_path = args.contract_report
    conversion_path = args.conversion_report
    reconstruction_path = args.reconstruction_report
    converted_path = args.converted_reconstruction_report

    if args.input_dir is not None:
        root = args.input_dir
        if contract_path is None and (root / "contract-report.json").exists():
            contract_path = root / "contract-report.json"
        if conversion_path is None and (root / "conversion-report.json").exists():
            conversion_path = root / "conversion-report.json"
        if reconstruction_path is None and (root / "reconstruction-report.json").exists():
            reconstruction_path = root / "reconstruction-report.json"
        if converted_path is None and (root / "converted-reconstruction-report.json").exists():
            converted_path = root / "converted-reconstruction-report.json"

    reports["contract"] = load_json(contract_path) if contract_path else None
    reports["conversion"] = load_json(conversion_path) if conversion_path else None
    reports["reconstruction"] = load_json(reconstruction_path) if reconstruction_path else None
    reports["convertedReconstruction"] = load_json(converted_path) if converted_path else None

    aggregate = aggregate_reports(reports)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(aggregate, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(aggregate, indent=2))
    return 0 if aggregate["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
