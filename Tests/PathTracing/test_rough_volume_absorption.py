import unittest

import numpy as np

from validate_rough_volume_absorption import assess
from volume_absorption_fixture import COLOR


class RoughVolumeAbsorptionTests(unittest.TestCase):
    def setUp(self):
        self.distance = np.ones((64, 64, 4))
        self.distance[..., 2] = 2
        self.transmittance = np.ones_like(self.distance)
        self.transmittance[..., :3] = np.square(COLOR)
        self.status = np.zeros_like(self.distance)

    def test_valid(self):
        self.assertEqual(assess(self.distance, self.transmittance, self.status)["completedPaths"], 4096)

    def test_wrong_absorption(self):
        self.transmittance[..., 0] += .01
        with self.assertRaises(ValueError):
            assess(self.distance, self.transmittance, self.status)

    def test_medium_error(self):
        self.status[0, 0, 0] = 2
        with self.assertRaises(ValueError):
            assess(self.distance, self.transmittance, self.status)

    def test_reject_only_is_not_success(self):
        self.status[..., 0] = 7
        with self.assertRaises(ValueError):
            assess(self.distance, self.transmittance, self.status)

    def test_nan(self):
        self.distance[0, 0, 2] = np.nan
        with self.assertRaises(ValueError):
            assess(self.distance, self.transmittance, self.status)

    def test_mismatch(self):
        with self.assertRaises(ValueError):
            assess(self.distance, self.transmittance[:32], self.status)


if __name__ == "__main__":
    unittest.main()
