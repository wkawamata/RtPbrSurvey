import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from rough_transmission_fixture import write_rough_fixture
from validate_rough_transmission import assess_furnace, furnace_reference


class RoughTransmissionFixtureTests(unittest.TestCase):
    def test_furnace_smooth_and_analytic_rough_limits(self):
        self.assertEqual(furnace_reference(0), 1)
        self.assertAlmostEqual(furnace_reference(1), 1 - np.log(2), places=9)
        self.assertGreater(furnace_reference(.5), furnace_reference(1))

    def test_furnace_rejects_broken_energy_or_missing_input(self):
        expected = furnace_reference(.5)
        assess_furnace(np.full((128, 128, 3), expected), .5, 128)
        for image in (np.zeros((128, 128, 3)), np.ones((128, 128, 3)) * 2, np.zeros((0, 0, 3))):
            with self.assertRaises(ValueError):
                assess_furnace(image, .5, 128)

    def test_review_preserves_three_distinct_roughnesses(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            scene = write_rough_fixture(folder)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            self.assertEqual(scene["sceneId"], "rough-thin-transmission")
            self.assertEqual([material["pbrMetallicRoughness"]["roughnessFactor"]
                              for material in gltf["materials"]], [0, 0.25, 0.6])
            self.assertNotIn("textures", gltf)
            for material in gltf["materials"]:
                self.assertEqual(material["pbrMetallicRoughness"]["metallicFactor"], 0)
                self.assertEqual(material["extensions"]["KHR_materials_transmission"], {"transmissionFactor": 1})
            self.assertEqual(len(gltf["scenes"][0]["nodes"]), 3)


if __name__ == "__main__":
    unittest.main()
