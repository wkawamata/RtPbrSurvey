import unittest
import numpy as np
from validate_object_motion import expected_motion


class ObjectMotionTests(unittest.TestCase):
    def metadata(self, delta):
        current = np.eye(4)
        current[0, 3] = delta
        return dict(singleInstanceWorld=current.tolist(), singleInstancePreviousWorld=np.eye(4).tolist(),
                    viewProjection=np.eye(4).tolist(), previousViewProjection=np.eye(4).tolist())

    def test_static(self):
        np.testing.assert_array_equal(expected_motion(self.metadata(0), np.zeros((2, 3)), 0), np.zeros((2, 2)))

    def test_positive_and_negative(self):
        for delta in (.05, -.05):
            np.testing.assert_allclose(expected_motion(self.metadata(delta), np.zeros((1, 3)), delta), [[-delta, 0]])

    def test_wrong_delta_rejected(self):
        with self.assertRaises(ValueError):
            expected_motion(self.metadata(.05), np.zeros((1, 3)), -.05)

    def test_rotation_rejected(self):
        meta = self.metadata(0)
        meta['singleInstanceWorld'][0][0] = 2
        with self.assertRaises(ValueError):
            expected_motion(meta, np.zeros((1, 3)), 0)

    def test_camera_motion_rejected(self):
        meta = self.metadata(0)
        meta['previousViewProjection'][0][3] = .1
        with self.assertRaises(ValueError):
            expected_motion(meta, np.zeros((1, 3)), 0)

    def test_nonfinite_rejected(self):
        meta = self.metadata(0)
        meta['singleInstanceWorld'][0][3] = float('nan')
        with self.assertRaises(ValueError):
            expected_motion(meta, np.zeros((1, 3)), 0)

    def test_existing_orientation_preserved(self):
        meta = self.metadata(.05)
        for name in ('singleInstanceWorld', 'singleInstancePreviousWorld'):
            meta[name][1][1] = 0
            meta[name][1][2] = 1
            meta[name][2][1] = -1
            meta[name][2][2] = 0
        np.testing.assert_allclose(expected_motion(meta, np.zeros((1, 3)), .05), [[-.05, 0]])


if __name__ == '__main__':
    unittest.main()
