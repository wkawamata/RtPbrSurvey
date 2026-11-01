import unittest
import numpy as np

from validate_gltf_volume import reference_tetra_lengths
from validate_smooth_volume_paths import reference
from sphere_mesh_reference import prepare_triangles, intersect_triangles


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

    def test_straight_slab_thickness(self):
        distance, status, outgoing = reference(self.eye, self.direction, 960, 540, ior=1, shape="slab")
        self.assertAlmostEqual(distance[2], .7/abs(self.direction[2]), places=10)
        np.testing.assert_array_equal(status, [0,0,0])
        np.testing.assert_allclose(outgoing, self.direction, atol=1e-10)

    def test_straight_prism_axis(self):
        distance, status, _ = reference(np.array([0.,0.,-5.]), np.array([0.,0.,1.]), 960, 540, ior=1, shape="prism")
        np.testing.assert_allclose(distance, [1,1,1], atol=1e-10)
        np.testing.assert_array_equal(status, [0,0,0])

    def test_triangle_intersection(self):
        geometry = prepare_triangles([[-1,-1,0], [1,-1,0], [0,1,0]], [0,1,2])
        t, face = intersect_triangles(np.array([0.,0.,-1.]), np.array([0.,0.,1.]), -1, False, geometry)
        self.assertEqual(face, 0)
        self.assertEqual(t, 1)
        with self.assertRaises(ValueError):
            intersect_triangles(np.array([0.,0.,-1.]), np.array([0.,0.,1.]), 0, False, geometry)

    def test_triangle_miss(self):
        geometry = prepare_triangles([[-1,-1,0], [1,-1,0], [0,1,0]], [0,1,2])
        with self.assertRaises(ValueError):
            intersect_triangles(np.array([5.,0.,-1.]), np.array([0.,0.,1.]), -1, False, geometry)


if __name__ == '__main__':
    unittest.main()
