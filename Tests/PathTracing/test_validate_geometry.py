import unittest
import numpy as np
from validate_geometry import expected_normal, surface_frame


class SurfaceFrameTests(unittest.TestCase):
    def test_orthonormal_frame(self):
        for mirrored in (False, True):
            for angle in (0, 30, -15):
                n, t, b = surface_frame(mirrored, angle)
                np.testing.assert_allclose(np.array([n, t, b]) @ np.array([n, t, b]).T, np.eye(3), atol=1e-12)

    def test_normal_map_changes_normal(self):
        self.assertGreater(np.linalg.norm(expected_normal(True)-expected_normal(False)), .3)

    def test_rotation_changes_normal(self):
        self.assertGreater(np.linalg.norm(expected_normal(False, angle=30)-expected_normal(False, angle=-15)), .5)

    def test_mirroring_changes_frame_handedness(self):
        self.assertLess(np.linalg.det(np.array(surface_frame(False))), 0)
        self.assertGreater(np.linalg.det(np.array(surface_frame(True))), 0)


if __name__ == '__main__':
    unittest.main()
