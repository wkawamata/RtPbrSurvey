import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from metallic_transmission_fixture import write_metallic_fixture
from validate_metallic_transmission import assess_mixture, assess_metallic_guide


class MetallicTransmissionTests(unittest.TestCase):
    def test_opaque_control_has_no_transmission_extension(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            write_metallic_fixture(folder, metallic=1, transmission=0, opaque=True)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            self.assertNotIn("extensions", gltf["materials"][0])
            self.assertNotIn("KHR_materials_transmission", gltf["extensionsRequired"])
            self.assertEqual(gltf["materials"][0]["pbrMetallicRoughness"]["metallicFactor"], 1)

    def test_metal_endpoint_requires_white_reflection(self):
        assessment, _ = assess_mixture(np.ones((128, 128, 4)), 1, 128)
        self.assertEqual(assessment["error"], [0, 0, 0])
        with self.assertRaises(ValueError):
            assess_mixture(np.zeros((128, 128, 4)), 1, 128)

    def test_nonfinite_or_missing_buffers_are_rejected(self):
        for image in (np.full((128, 128, 4), np.nan), np.zeros((0, 0, 4))):
            with self.assertRaises(ValueError):
                assess_mixture(image, .5, 128)
            with self.assertRaises(ValueError):
                assess_metallic_guide(image, .5)

    def test_wrong_metallic_channel_is_rejected(self):
        with self.assertRaises(ValueError):
            assess_metallic_guide(np.ones((128, 128, 4)), .5)

    def test_saved_review_materials_keep_three_metallic_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            scene = write_metallic_fixture(folder, review=True)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            self.assertEqual(scene["sceneId"], "metallic-thin-transmission")
            self.assertEqual([material["pbrMetallicRoughness"]["metallicFactor"]
                              for material in gltf["materials"]], [0, .5, 1])
            self.assertTrue(all(material["extensions"]["KHR_materials_transmission"] == {"transmissionFactor": 1}
                                for material in gltf["materials"]))


if __name__ == "__main__":
    unittest.main()
