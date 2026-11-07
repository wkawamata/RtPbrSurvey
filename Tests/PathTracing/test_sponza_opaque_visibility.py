"""Guard geometry exclusion, index remapping, and GPU-log parsing."""
import json
from pathlib import Path
import tempfile
import unittest

from validate_part1 import write_json
from validate_sponza_opaque_visibility import gpu_metrics, opaque_variant


class OpaqueSponzaFixture(unittest.TestCase):
    def test_removes_mask_primitives_without_changing_kept_geometry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "source.gltf"
            (root / "mesh.bin").write_bytes(b"fixture")
            destination = root / "derived/opaque.gltf"
            write_json(source, dict(materials=[dict(alphaMode="MASK"), dict(name="kept")],
                meshes=[dict(primitives=[dict(material=0, attributes={"POSITION":0})]),
                        dict(primitives=[dict(material=0, attributes={"POSITION":1}),
                                         dict(material=1, attributes={"POSITION":2}, indices=3)])],
                nodes=[dict(mesh=0, children=[1], translation=[1,2,3]), dict(mesh=1)],
                buffers=[dict(uri="mesh.bin", byteLength=7)]))
            original = source.read_bytes()
            result = opaque_variant(source, destination)
            actual = json.loads(destination.read_text(encoding="utf-8"))
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(result["keptPrimitives"], 1)
            self.assertEqual(len(result["removedPrimitives"]), 2)
            self.assertEqual(actual["nodes"][0], dict(children=[1], translation=[1,2,3]))
            self.assertEqual(actual["nodes"][1]["mesh"], 0)
            self.assertEqual(actual["meshes"][0]["primitives"],
                             [dict(material=0, attributes={"POSITION":2}, indices=3)])
            self.assertEqual((destination.parent / actual["buffers"][0]["uri"]).resolve(), root / "mesh.bin")

    def test_gpu_samples_ignore_warmup_and_preserve_pass_names(self):
        result = gpu_metrics("[GPU] Frame 10: latest completed total 90.0 ms\n"
            "[GPU Pass] Light Pass: 80.0 ms\n"
            "[GPU] Frame 60: latest completed total 4.0 ms\n"
            "[GPU Pass] Diffuse Environment Visibility: 2.0 ms\n"
            "[GPU] Frame 70: latest completed total 6.0 ms\n"
            "[GPU Pass] Diffuse Environment Visibility: 4.0 ms\n")
        self.assertEqual(result["Total"]["samples"], 2)
        self.assertEqual(result["Total"]["medianMs"], 5)
        self.assertEqual(result["Diffuse Environment Visibility"]["medianMs"], 3)
        self.assertNotIn("Light Pass", result)


if __name__ == "__main__":
    unittest.main()
