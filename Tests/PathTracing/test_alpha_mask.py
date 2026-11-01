import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from alpha_mask_fixture import PLATE, write_alpha_fixture, write_visibility_fixture
from validate_alpha_mask import assess_primary, visibility_patch


class AlphaMaskTests(unittest.TestCase):
    def test_opaque_ignores_alpha_and_constant_equality_passes(self):
        image = np.empty((128, 128, 3))
        image[:] = PLATE
        assess_primary(image, dict(mode="OPAQUE", factor=0))
        assess_primary(image, dict(texture=False, factor=.5, cutoff=.5))

    def test_wrong_coverage_or_missing_data_is_rejected(self):
        with self.assertRaises(ValueError):
            assess_primary(np.zeros((128, 128, 3)), dict(mode="OPAQUE"))
        for image in (np.full((128, 128, 3), np.nan), np.zeros((0, 0, 3))):
            with self.assertRaises(ValueError):
                assess_primary(image, {})
            with self.assertRaises(ValueError):
                visibility_patch(image)

    def test_review_keeps_opaque_mask_and_transmission_distinct(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            scene = write_alpha_fixture(folder, review=True)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            self.assertEqual(scene["sceneId"], "alpha-mask-thin-transmission")
            self.assertEqual([material["alphaMode"] for material in gltf["materials"]], ["OPAQUE", "MASK", "MASK"])
            self.assertNotIn("KHR_materials_transmission", gltf["materials"][1]["extensions"])
            self.assertEqual(gltf["materials"][2]["extensions"]["KHR_materials_transmission"]["transmissionFactor"], 1)

    def test_masked_emitter_keeps_emission_but_has_zero_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            write_visibility_fixture(folder, "clear", masked_emitter=True)
            gltf = json.loads((folder / "emitter.gltf").read_text(encoding="utf-8"))
            material = gltf["materials"][0]
            self.assertEqual(material["alphaMode"], "MASK")
            self.assertEqual(material["pbrMetallicRoughness"]["baseColorFactor"][3], 0)
            self.assertGreater(max(material["emissiveFactor"]), 0)

    def test_backface_emitter_preserves_coverage_and_height(self):
        for double_sided in (False, True):
            with tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary) / "fixture"
                scene = write_visibility_fixture(folder, "clear", emitter_backface=double_sided)
                gltf = json.loads((folder / "emitter.gltf").read_text(encoding="utf-8"))
                material = gltf["materials"][0]
                self.assertEqual(material["alphaMode"], "MASK")
                self.assertIs(material["doubleSided"], double_sided)
                self.assertEqual(material["pbrMetallicRoughness"]["baseColorFactor"][3], 1)
                node = next(node for node in scene["nodes"] if node["id"] == "emitter")
                self.assertEqual(node["rotation"], [1, 0, 0, 0])
                self.assertEqual(-3 + node["translation"][1], 3)


if __name__ == "__main__":
    unittest.main()
