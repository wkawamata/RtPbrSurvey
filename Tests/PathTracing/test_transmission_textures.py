import unittest
import json
from pathlib import Path
import tempfile

import numpy as np

from validate_transmission_textures import fresnel, mean_gate, srgb_to_linear
from transmission_texture_fixture import write_fixture


class TransmissionTextureReferenceTests(unittest.TestCase):
    def test_fixture_places_background_behind_pane_after_handedness_conversion(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "fixture"
            scene = write_fixture(folder)
            gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
            pane, background = gltf["nodes"]
            self.assertEqual(scene["camera"]["position"], [0, 0, -5])
            self.assertGreater(-background["translation"][2], -pane["translation"][2])
            preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
            self.assertEqual(preset["backBufferClearColor"], [0.8, 0.6, 0.4, 1])

    def test_srgb_reference(self):
        np.testing.assert_allclose(srgb_to_linear([0, 128, 255]), [0, 0.21586050011389926, 1], atol=1e-12)

    def test_fresnel_reference(self):
        self.assertAlmostEqual(float(fresnel(1, 1.5)), 0.04)
        self.assertEqual(float(fresnel(0.7, 1)), 0)

    def test_gate_accepts_deterministic_reference(self):
        mean_gate(np.full((8, 8, 3), 0.25), [0.25] * 3, 16, [0.5] * 3, deterministic=True)

    def test_gate_rejects_wrong_gamma_or_channel(self):
        for wrong in (0.5, 0.8):
            with self.assertRaises(ValueError):
                mean_gate(np.full((16, 16, 3), wrong), [0.21586] * 3, 64, [1] * 3)

    def test_gate_rejects_nonfinite_or_missing_data(self):
        for patch in (np.full((8, 8, 3), np.nan), np.zeros((0, 0, 3)), np.zeros((8, 8))):
            with self.assertRaises(ValueError):
                mean_gate(patch, [0] * 3, 16, [1] * 3)
        with self.assertRaises(ValueError):
            mean_gate(np.zeros((8, 8, 3)), [0] * 3, 0, [1] * 3)


if __name__ == "__main__":
    unittest.main()
