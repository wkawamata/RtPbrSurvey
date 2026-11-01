import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from transmission_texture_fixture import BACKGROUND
from validate_volume_absorption import assess, assess_relationships, expected_transmittance, capture_resource
from validate_volume_boundary import reference_rays
from volume_absorption_fixture import COLOR, write_absorption_fixture


class VolumeAbsorptionTests(unittest.TestCase):
    def test_path_pairs_capture_single_frame_not_history_average(self):
        for kind in ("distance", "paired-transmittance", "status", "transmittance", "radiance"):
            self.assertEqual(capture_resource(kind), "CurrentFrameRadiance")
        self.assertEqual(capture_resource("fresnel"), "SceneColor")

    def test_beer_lambert_edges_and_double_distance(self):
        length = np.array([0, 1, 2])
        value = expected_transmittance(length, [0, .5, 1], 1)
        np.testing.assert_array_equal(value[0], [1, 1, 1])
        np.testing.assert_allclose(value[2], value[1]**2)
        np.testing.assert_array_equal(expected_transmittance(length, [0, .5, 1], None), np.ones((3, 3)))
        for distance in (0, -1, float("nan")):
            with self.assertRaises(ValueError):
                expected_transmittance(length, COLOR, distance)

    def test_oblique_distance_not_projected_thickness(self):
        scene = dict(camera=dict(position=[2, 0, -5], target=[0, 0, 0], up=[0, 1, 0]))
        _, rays, roi = reference_rays(scene, 256, 256)
        image = np.zeros((256, 256, 4))
        image[roi][..., :3] = expected_transmittance(1/rays[..., 2], COLOR, 1)
        assessment, _ = assess(image, scene, {}, {}, "transmittance", "test", {})
        json.dumps(assessment)
        image[roi][..., :3] = COLOR
        with self.assertRaises(ValueError):
            assess(image, scene, {}, {}, "transmittance", "test", {})

    def test_paired_paths_do_not_use_mean_distance(self):
        scene = dict(camera=dict(position=[0, 0, -5], target=[0, 0, 0], up=[0, 1, 0]))
        _, _, roi = reference_rays(scene, 256, 256)
        length = np.linspace(0, 4, 1024).reshape(32, 32)
        pair = dict(case="length", length=length, scene=scene, preset=dict(pathTracing=dict(randomSeed=7)))
        preset = dict(pathTracing=dict(randomSeed=7, debugOutput=8))
        image = np.zeros((256, 256, 4))
        image[roi][..., :3] = expected_transmittance(length, COLOR, 1)
        assess(image, scene, preset, {}, "paired-transmittance", "test", dict(reflection=pair))
        image[roi][..., :3] = expected_transmittance(np.array(length.mean()), COLOR, 1)
        with self.assertRaises(ValueError):
            assess(image, scene, preset, {}, "paired-transmittance", "test", dict(reflection=pair))
        preset["pathTracing"]["randomSeed"] = 8
        with self.assertRaises(ValueError):
            assess(image, scene, preset, {}, "paired-transmittance", "test", dict(reflection=pair))

    def test_relationships_require_all_controls_and_reject_bias(self):
        single = np.full((4, 4, 3), COLOR)*BACKGROUND
        patches = dict(normal=single, double=np.full((4, 4, 3), np.array(COLOR)**2)*BACKGROUND,
            oblique=single, **{"bias-control": single.copy(), "nrd-fallback": single.copy()})
        assess_relationships(patches)
        patches["bias-control"] += .01
        with self.assertRaises(ValueError):
            assess_relationships(patches)
        with self.assertRaises(ValueError):
            assess_relationships({})

    def test_fixture_keeps_white_boundary_and_linear_absorption(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)/"fixture"
            scene, preset = write_absorption_fixture(folder, distance=2, thickness=2, debug=8)
            material = scene["materials"][0]
            self.assertEqual(material["baseColor"], [1, 1, 1, 1])
            self.assertEqual(material["transmission"]["tint"], [1, 1, 1])
            self.assertEqual(material["transmission"]["volume"]["attenuationColor"], list(COLOR))
            self.assertEqual(material["transmission"]["volume"]["attenuationDistance"], 2)
            self.assertEqual(scene["nodes"][-1]["scale"][2], 1)
            self.assertEqual(preset["pathTracing"]["debugOutput"], 8)


if __name__ == "__main__":
    unittest.main()
