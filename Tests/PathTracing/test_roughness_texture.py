import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from roughness_texture_fixture import write_roughness_texture_fixture
from validate_roughness_texture import assess_guide


class RoughnessTextureTests(unittest.TestCase):
    def test_guide_reads_linear_g_and_factor(self):
        image = np.zeros((128, 128, 4))
        image[..., 3] = .5 * 128 / 255
        self.assertEqual(len(assess_guide(image, .5, False)), 4)

    def test_guide_rejects_gamma_wrong_channel_and_ignored_factor(self):
        for wrong in (32 / 255, 1, .21586, 128 / 255):
            image = np.zeros((128, 128, 4))
            image[..., 3] = wrong
            with self.assertRaises(ValueError):
                assess_guide(image, .5, False)

    def test_guide_rejects_missing_and_nonfinite_data(self):
        for image in (np.zeros((0, 0, 4)), np.zeros((128, 128, 3)), np.full((128, 128, 4), np.nan)):
            with self.assertRaises(ValueError):
                assess_guide(image, 1, False)

    def test_review_uses_texture_for_two_panels_and_constant_control(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            scene = write_roughness_texture_fixture(folder, review=True)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            self.assertEqual(scene["sceneId"], "roughness-textured-thin-transmission")
            pbrs = [material["pbrMetallicRoughness"] for material in gltf["materials"]]
            self.assertEqual([pbr["roughnessFactor"] for pbr in pbrs], [1, .5, .5])
            self.assertEqual(pbrs[0]["metallicRoughnessTexture"]["index"], 0)
            self.assertEqual(pbrs[1]["metallicRoughnessTexture"]["index"], 0)
            self.assertNotIn("metallicRoughnessTexture", pbrs[2])
            self.assertTrue(all(pbr["metallicFactor"] == 0 for pbr in pbrs))
            self.assertEqual([node["name"] for node in gltf["nodes"]],
                             ["Roughness texture G x 1.0", "Roughness texture G x 0.5", "Constant roughness 0.5"])
            self.assertTrue((folder / "roughness.png").is_file())


if __name__ == "__main__":
    unittest.main()
