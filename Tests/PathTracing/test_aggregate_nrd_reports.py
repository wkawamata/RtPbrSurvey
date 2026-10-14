import json
import tempfile
import unittest
from pathlib import Path

from aggregate_nrd_reports import aggregate_reports, validate_conversion_report


def make_reports():
    contract = {
        "schemaVersion": 1,
        "status": "passed",
        "hostOnlyCategories": ["snapshot-token", "scene-range-proof", "trace-provenance"],
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
        "gpuProofClaimed": False,
    }
    conversion = {
        "schemaVersion": 1,
        "status": "passed",
        "sourceReportSha256": "source",
        "exeSha256": "exe",
        "shaderSha256": "shader",
        "scriptSha256": "script",
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
        "records": [
            {"variant": "hit-only", "hypotheticalEnvironment": False, "d3d12Errors": 0,
             "inputSha256": "a", "outputSha256": "b", "assessment": {"inputReady": False, "rejectedPixels": 0}},
            {"variant": "hypothetical-environment", "hypotheticalEnvironment": True, "d3d12Errors": 0,
             "inputSha256": "c", "outputSha256": "d", "assessment": {"inputReady": False, "rejectedPixels": 1}},
        ],
    }
    reconstruction = {
        "schemaVersion": 1,
        "status": "passed",
        "sourceReportSha256": "source",
        "exeSha256": "exe",
        "scriptSha256": "script",
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
        "records": [
            {"variant": "hit-only", "mode": "3x3", "hypotheticalEnvironment": False, "d3d12Errors": 0, "shaderSha256": "s"},
            {"variant": "hit-only", "mode": "5x5", "hypotheticalEnvironment": False, "d3d12Errors": 0, "shaderSha256": "s"},
            {"variant": "hypothetical-environment", "mode": "3x3", "hypotheticalEnvironment": True, "d3d12Errors": 0, "shaderSha256": "s"},
            {"variant": "hypothetical-environment", "mode": "5x5", "hypotheticalEnvironment": True, "d3d12Errors": 0, "shaderSha256": "s"},
        ],
    }
    converted = {
        "schemaVersion": 1,
        "status": "passed",
        "sourceReportSha256": "source",
        "conversionReportSha256": "conv",
        "inputReady": False,
        "runtimeGeometrySnapshotVerified": False,
        "records": [
            {"variant": "hit-only", "mode": "3x3", "hypotheticalEnvironment": False, "d3d12Errors": 0,
             "lobes": {"Diffuse": {"maximumPhysicalRoundTripError": 1e-7},
                       "Specular": {"maximumPhysicalRoundTripError": 2e-7}}},
            {"variant": "hit-only", "mode": "5x5", "hypotheticalEnvironment": False, "d3d12Errors": 0,
             "lobes": {"Diffuse": {"maximumPhysicalRoundTripError": 1e-7},
                       "Specular": {"maximumPhysicalRoundTripError": 2e-7}}},
            {"variant": "hypothetical-environment", "mode": "3x3", "hypotheticalEnvironment": True, "d3d12Errors": 0,
             "lobes": {"Diffuse": {"maximumPhysicalRoundTripError": 1e-7},
                       "Specular": {"maximumPhysicalRoundTripError": 2e-7}}},
            {"variant": "hypothetical-environment", "mode": "5x5", "hypotheticalEnvironment": True, "d3d12Errors": 0,
             "lobes": {"Diffuse": {"maximumPhysicalRoundTripError": 1e-7},
                       "Specular": {"maximumPhysicalRoundTripError": 2e-7}}},
        ],
    }
    return contract, conversion, reconstruction, converted


class AggregateNrdReportsTests(unittest.TestCase):
    def test_all_reports_pass(self):
        contract, conversion, reconstruction, converted = make_reports()
        result = aggregate_reports({
            "contract": contract,
            "conversion": conversion,
            "reconstruction": reconstruction,
            "convertedReconstruction": converted,
        })
        self.assertEqual(result["status"], "passed")
        self.assertFalse(result["inputReady"])
        self.assertFalse(result["runtimeGeometrySnapshotVerified"])
        self.assertEqual(result["productionRuntimeStatus"], "unavailable-not-integrated")
        self.assertEqual(result["statuses"]["productionRuntime"]["available"], False)

    def test_missing_report_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        result = aggregate_reports({"contract": None, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("missing required report: contract" in failure for failure in result["failures"]))

    def test_duplicate_variant_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        conversion["records"][1]["variant"] = "hit-only"
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("duplicate" in failure for failure in result["failures"]))

    def test_stale_source_hash_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        converted["sourceReportSha256"] = "stale"
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("source hash" in failure or "source/report hash" in failure for failure in result["failures"]))

    def test_d3d12_error_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        conversion["records"][0]["d3d12Errors"] = 1
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("D3D12" in failure for failure in result["failures"]))

    def test_missing_coverage_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        converted["records"].pop()
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("expected" in failure.lower() or "missing expected" in failure.lower()
                            for failure in result["failures"]))

    def test_host_only_gpu_proof_claim_is_rejected(self):
        contract, conversion, reconstruction, converted = make_reports()
        contract["gpuProofClaimed"] = True
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("GPU proof" in failure for failure in result["failures"]))

    def test_production_runtime_remains_unavailable(self):
        contract, conversion, reconstruction, converted = make_reports()
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertFalse(result["statuses"]["productionRuntime"]["available"])
        self.assertFalse(result["statuses"]["productionRuntime"]["integrated"])

    def test_hypothetical_environment_is_distinguished(self):
        contract, conversion, reconstruction, converted = make_reports()
        result = aggregate_reports({"contract": contract, "conversion": conversion,
                                     "reconstruction": reconstruction, "convertedReconstruction": converted})
        self.assertEqual(result["statuses"]["hypotheticalEnvironment"]["hypothetical"], True)
        self.assertFalse(result["statuses"]["hypotheticalEnvironment"]["runtimeRangeProof"])

    def test_cli_temporary_directory_round_trip(self):
        import subprocess

        contract, conversion, reconstruction, converted = make_reports()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "contract-report.json").write_text(json.dumps(contract), encoding="utf-8")
            (root / "conversion-report.json").write_text(json.dumps(conversion), encoding="utf-8")
            (root / "reconstruction-report.json").write_text(json.dumps(reconstruction), encoding="utf-8")
            (root / "converted-reconstruction-report.json").write_text(json.dumps(converted), encoding="utf-8")
            output = root / "aggregate.json"
            script = Path(__file__).resolve().parent / "aggregate_nrd_reports.py"
            result = subprocess.run(
                [str(Path(__import__("sys").executable)), str(script), "--input-dir", str(root), "--output", str(output)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            aggregate = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(aggregate["status"], "passed")


if __name__ == "__main__":
    unittest.main()
