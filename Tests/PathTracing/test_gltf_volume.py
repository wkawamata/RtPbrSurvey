import unittest
import numpy as np
import json

from validate_gltf_volume import reference_tetra_lengths, assess_status


class TetraReferenceTests(unittest.TestCase):
    def test_central_unit_chord(self):
        length, valid, _ = reference_tetra_lengths(np.array([0, 0, -5]), np.array([[0, 0, 1.0]]))
        self.assertTrue(valid.all())
        np.testing.assert_allclose(length, [1])

    def test_parallel_miss(self):
        length, valid, _ = reference_tetra_lengths(np.array([-2, 0, -5]), np.array([[0, 0, 1.0]]))
        self.assertFalse(valid.any())
        np.testing.assert_array_equal(length, [0])

    def test_away_from_solid(self):
        length, valid, _ = reference_tetra_lengths(np.array([0, 0, -5]), np.array([[0, 0, -1.0]]))
        self.assertFalse(valid.any())
        np.testing.assert_array_equal(length, [0])

    def test_status_does_not_allow_interior_error(self):
        scene = dict(camera=dict(position=[0, 0, -5], target=[0, 0, 0], up=[0, 1, 0], verticalFovDegrees=40))
        preset = dict(shadow=dict(normalBias=.0001, rayTMin=.0001))
        image = np.zeros((128, 128, 3))
        self.assertEqual(assess_status(image, scene, preset)["grazingMissPixels"], 0)
        image[64, 64, :2] = [1, 1]
        with self.assertRaises(ValueError):
            assess_status(image, scene, preset)
        image[64, 64, :2] = [3, 1]
        with self.assertRaises(ValueError):
            assess_status(image, scene, preset)

    def test_grazing_assessment_is_serializable(self):
        scene = dict(camera=dict(position=[0, 0, -5], target=[0, 0, 0], up=[0, 1, 0], verticalFovDegrees=40))
        preset = dict(shadow=dict(normalBias=10, rayTMin=10))
        image = np.zeros((128, 128, 3))
        image[64, 64, :2] = [1, 1]
        assessment = assess_status(image, scene, preset)
        self.assertEqual(assessment["grazingMissPixels"], 1)
        json.dumps(assessment)


if __name__ == "__main__":
    unittest.main()
