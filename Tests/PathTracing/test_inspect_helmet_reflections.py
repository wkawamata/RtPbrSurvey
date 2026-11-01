import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from inspect_helmet_reflections import CASES, SCENE, difference, make_case, validate_state


class HelmetReflectionTests(unittest.TestCase):
    def test_case_list_is_unique(self):
        self.assertEqual(len(CASES), 5)
        self.assertEqual(len({c[0] for c in CASES}), 5)

    def test_variant_preserves_original_material_except_normal(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            scene, preset = make_case(folder, 8, True, True)
            asset = json.loads((folder / "helmet.gltf").read_text(encoding="utf-8"))
            self.assertNotIn("normalTexture", asset["materials"][0])
            self.assertIn("metallicRoughnessTexture", asset["materials"][0]["pbrMetallicRoughness"])
            self.assertEqual(scene["environment"]["groundColor"], [0, 0, 0])
            self.assertEqual(preset["pathTracing"]["maxBounces"], 8)
            self.assertFalse(Path(asset["images"][0]["uri"]).is_absolute())
            self.assertTrue((folder / asset["images"][0]["uri"]).is_file())
            self.assertGreater(json.loads(SCENE.read_text(encoding="utf-8"))["environment"]["groundColor"][0], 0)

    def test_difference_rejects_mismatch_and_nonfinite(self):
        a = np.zeros((2, 2, 4))
        self.assertTrue(difference(a, a)["exactEqual"])
        b = a.copy()
        b[0, 0, 0] = 1
        self.assertEqual(difference(a, b)["changedPixels"], 1)
        with self.assertRaises(ValueError):
            difference(a, b[:1])
        b[0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            difference(a, b)

    def test_missing_runtime_evidence_is_rejected(self):
        with self.assertRaises(ValueError):
            validate_state({}, {}, 4, 64)


if __name__ == "__main__":
    unittest.main()
