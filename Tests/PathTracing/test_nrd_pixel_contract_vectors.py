import json
import os
import copy
import math
import subprocess
import tempfile
import unittest
from pathlib import Path


CORPUS = Path(__file__).resolve().with_name("nrd_pixel_contract_vectors.json")
REQUIRED_HOST_ONLY = {"snapshot-token", "scene-range-proof", "trace-provenance"}


class NrdPixelContractVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(CORPUS.read_text(encoding="utf-8"))

    def test_schema_and_unique_ids(self):
        self.assertEqual(self.data["schemaVersion"], 1)
        self.assertEqual(self.data["kind"], "nrd-pixel-contract-vectors")
        vectors = self.data["vectors"]
        ids = [vector["id"] for vector in vectors]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertGreater(len(vectors), 0)

    def test_host_only_categories_are_explicit(self):
        self.assertEqual(set(self.data["hostOnlyCategories"]), REQUIRED_HOST_ONLY)
        host_only = [vector for vector in self.data["vectors"] if vector["hostOnly"]]
        self.assertEqual(len(host_only), 3)
        for vector in host_only:
            self.assertFalse(vector["gpuRepresentable"])
            self.assertEqual(vector["expectedProofDomain"], "host-only")

    def test_gpu_representable_vectors_are_not_host_only(self):
        gpu_vectors = [vector for vector in self.data["vectors"] if vector["gpuRepresentable"]]
        self.assertTrue(gpu_vectors)
        for vector in gpu_vectors:
            self.assertFalse(vector["hostOnly"])
            self.assertIn("expectedDisposition", vector)

    def test_partial_output_policy(self):
        for vector in self.data["vectors"]:
            if "partialOutputsOnFailure" in vector:
                self.assertFalse(vector["partialOutputsOnFailure"])

    def test_no_gpu_proof_is_claimed_for_host_only_vectors(self):
        for vector in self.data["vectors"]:
            if vector["hostOnly"]:
                self.assertNotEqual(vector.get("gpuRepresentation"), "represented")
                self.assertNotIn("gpuMeasured", vector)

    def test_corpus_case_ids_are_unique_and_stable(self):
        ids = [vector["id"] for vector in self.data["vectors"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertIn("cpu-pixel-convertible-base", ids)
        self.assertIn("cpu-snapshot-token-provenance", ids)

    def test_host_only_cases_are_not_gpu_representable(self):
        for vector in self.data["vectors"]:
            if vector["hostOnly"]:
                self.assertFalse(vector["gpuRepresentable"])
                self.assertNotEqual(vector.get("gpuRepresentation"), "represented")

    def test_normalized_numeric_values_are_finite(self):
        for vector in self.data["vectors"]:
            if vector.get("hostOnly"):
                continue
            for lobe in vector["lobes"]:
                for value in lobe["physical"] + lobe["materialFactor"] + lobe["distance"]:
                    self.assertTrue(isinstance(value, (int, float)))
                    self.assertTrue(math.isfinite(value))

    def test_incomplete_duplicate_or_changed_cases_fail_in_cpp(self):
        build = Path(os.environ.get("RTPBRSURVEY_BUILD_DIR", str(Path(__file__).resolve().parents[2]/"build")))
        executable = build/"Debug/RtPbrSurvey.NrdRelaxAdapterTests.exe"
        if not executable.is_file():
            self.skipTest("Build the CPU adapter test first")
        mutations = (
            lambda data: [case.pop("expectedDisposition", None) for case in data["vectors"] if not case["hostOnly"]],
            lambda data: data["vectors"].append(copy.deepcopy(data["vectors"][0])),
            lambda data: data["vectors"].pop(0),
            lambda data: data["vectors"][0].__setitem__("expectedDisposition", "InvalidData"),
            lambda data: data["vectors"][0]["expectedPacked"][0].__setitem__(0, 3),
            lambda data: data["vectors"][0].pop("expectedPacked"),
            lambda data: data["vectors"][0]["lobes"][0].__setitem__("physical", [1]),
            lambda data: data["vectors"][0]["lobes"][0]["materialFactor"].__setitem__(0, float("inf")),
        )
        with tempfile.TemporaryDirectory(prefix="nrd-cpu-corpus-") as temporary:
            for index, mutate in enumerate(mutations):
                data = copy.deepcopy(self.data)
                mutate(data)
                path = Path(temporary)/(str(index)+".json")
                path.write_text(json.dumps(data), encoding="utf-8")
                result = subprocess.run([str(executable), str(path)], capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_cpu_adapter_executes_every_applicable_case_by_id(self):
        build_roots = [
            Path(os.environ.get("RTPBRSURVEY_BUILD_DIR", "")),
            Path(__file__).resolve().parents[2] / "bin" / "x64" / "Debug",
            Path(__file__).resolve().parents[2] / "build" / "Debug",
        ]
        candidates = []
        for root in build_roots:
            if root:
                candidates.extend(root.glob("*NrdRelaxAdapterTests*.exe"))
        executable = next((path for path in candidates if path.is_file()), None)
        if executable is None:
            self.skipTest("NrdRelaxAdapterTests binary not available")
        import subprocess

        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for vector in self.data["vectors"]:
            if vector.get("hostOnly"):
                continue
            if "expectedDisposition" not in vector:
                continue
            self.assertIn(
                "Pixel contract vector case executed: {} -> {}".format(
                    vector["id"], vector["expectedDisposition"]
                ),
                result.stdout,
            )
        self.assertIn("executed=5 host-only=3", result.stdout)

    def test_cpu_adapter_binary_smoke_when_available(self):
        build_roots = [
            Path(os.environ.get("RTPBRSURVEY_BUILD_DIR", "")),
            Path(__file__).resolve().parents[2] / "bin" / "x64" / "Debug",
            Path(__file__).resolve().parents[2] / "build" / "Debug",
        ]
        candidates = []
        for root in build_roots:
            if root:
                candidates.extend(root.glob("*NrdRelaxAdapterTests*.exe"))
        executable = next((path for path in candidates if path.is_file()), None)
        if executable is None:
            self.skipTest("NrdRelaxAdapterTests binary not available")
        import subprocess

        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
