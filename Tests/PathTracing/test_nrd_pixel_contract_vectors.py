import json
import os
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
