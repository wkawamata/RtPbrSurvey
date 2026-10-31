"""Regression checks for the near-mirror diagnostic, not renderer correctness claims."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from validate_near_mirror import CASES, assess, fixture, reflection_quantile


class NearMirrorTests(unittest.TestCase):
    def test_analytic_quantiles(self):
        self.assertEqual(reflection_quantile(0, .5), 0)
        self.assertAlmostEqual(reflection_quantile(1, .5), 90)
        self.assertAlmostEqual(reflection_quantile(.00746155, .5, .001),
                               reflection_quantile(.03, .5, .001))
        self.assertGreater(reflection_quantile(.00746155, .5, .001),
                           17 * reflection_quantile(.00746155, .5))
        self.assertGreater(reflection_quantile(.06, .9), reflection_quantile(.06, .5))
        for r, q, floor in ((-1, .5, 0), (float("nan"), .5, 0), (0, 1, 0), (0, .5, -1)):
            with self.assertRaises(ValueError):
                reflection_quantile(r, q, floor)

    def images(self):
        baseline = np.zeros((360, 640, 4), dtype=np.float32)
        baseline[120:240, 319:321, :3] = .8
        images = {name: baseline.copy() for name, _ in CASES}
        for name in ("above-floor", "rough-control"):
            images[name][120:240, 318:322, :3] = .4
        return images

    def test_assessment_and_rejection(self):
        good = self.images()
        self.assertTrue(assess(good)["plateauExactlyEqual"])
        for mutation in ("missing", "nan", "dark", "plateau", "control", "energy"):
            images = copy.deepcopy(good)
            if mutation == "missing":
                del images["zero"]
            elif mutation == "nan":
                images["zero"][0, 0, 0] = np.nan
            elif mutation == "dark":
                images["zero"][:] = 0
            elif mutation == "plateau":
                images["helmet-median"][0, 0, 0] = .1
            elif mutation == "control":
                images["above-floor"] = images["zero"].copy()
            else:
                images["zero"][0, 0, 0] = 2
            with self.assertRaises(ValueError, msg=mutation):
                assess(images)

    def test_fixture_is_reflection_only(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp) / "case"
            fixture(folder, .00746155)
            scene = json.loads((folder / "scene.json").read_text(encoding="utf-8"))
            preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
            model = json.loads((folder / "target.gltf").read_text(encoding="utf-8"))
            self.assertEqual(scene["camera"]["position"], [0, 0, -5])
            self.assertEqual(model["accessors"][3]["min"][2], 6)
            self.assertEqual(model["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"], .00746155)
            self.assertFalse(preset["pathTracing"]["environmentEnabled"])
            self.assertEqual(preset["pathTracing"]["emissiveSamplingMode"], 0)
            self.assertEqual(preset["pathTracing"]["maxBounces"], 2)
            self.assertFalse(preset["pathTracing"]["nrdPreview"])


if __name__ == "__main__":
    unittest.main()
