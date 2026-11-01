import unittest
import numpy as np

from validate_gltf_volume import reference_tetra_lengths
from validate_smooth_volume_paths import reference


class SmoothVolumePathTests(unittest.TestCase):
    def setUp(self):
        self.eye = np.array([3., 2., -5.])
        self.direction = -self.eye / np.linalg.norm(self.eye)

    def test_equal_ior_is_straight(self):
        distance, status, outgoing = reference(self.eye, self.direction, 960, 540, ior=1)
        length, valid, _ = reference_tetra_lengths(self.eye, self.direction[None, :])
        self.assertTrue(valid[0])
        np.testing.assert_allclose(distance, [1, 1, length[0]], atol=1e-10)
        np.testing.assert_array_equal(status, [0, 0, 0])
        np.testing.assert_allclose(outgoing, self.direction, atol=1e-10)

    def test_scaled_geometric_distance(self):
        for scale in (.5, 1.5):
            distance, _, _ = reference(self.eye, self.direction, 960, 540, ior=1, scale=scale)
            length, _, _ = reference_tetra_lengths(self.eye/scale, self.direction[None, :])
            self.assertAlmostEqual(distance[2], length[0]*scale, places=10)

    def test_budget_is_distinct_from_missing_exit(self):
        distance, status, _ = reference(self.eye, self.direction, 960, 540, ior=1, bounces=2)
        np.testing.assert_array_equal(status, [3, 1, 0])
        np.testing.assert_array_equal(distance[:2], [1, 0])

    def test_invalid_parameters(self):
        for params in (dict(ior=0), dict(ior=float('nan')), dict(scale=0), dict(bounces=17)):
            with self.assertRaises(ValueError):
                reference(self.eye, self.direction, 960, 540, **params)


if __name__ == '__main__':
    unittest.main()
