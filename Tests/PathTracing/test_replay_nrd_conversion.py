import unittest
import numpy as np

from replay_nrd_conversion import assess_conversion


class CapturedConversionTests(unittest.TestCase):
    def fixture(self):
        inputs = np.zeros((1, 2, 7, 4), dtype=np.float32)
        inputs[0, 0, 1, 3] = .35
        inputs[0, 0, 2, :3] = 1
        inputs[0, 0, 4, :3] = [1, 2, 3]
        inputs[0, 0, 5, :3] = [.5, 1, 3]
        inputs[0, 0, 6] = [2, 4, 1, 0]
        output = np.zeros((1, 2, 4, 4), dtype=np.float32)
        output[0, 0, 0] = [2, 4, 6, 2]
        output[0, 0, 1] = [1, 2, 6, 4]
        output[0, 0, 2, :3] = .5
        output[0, 0, 3, :3] = .5
        output[0, 1, 2:, 3] = 4
        return inputs, output

    def test_physical_roundtrip_and_excluded_pixel(self):
        inputs, output = self.fixture()
        result = assess_conversion(inputs, output)
        self.assertEqual(result['acceptedPixels'], 1)
        self.assertFalse(result['inputReady'])

    def test_rgb_or_distance_changes_are_rejected(self):
        for channel in (0, 3):
            inputs, output = self.fixture()
            output[0, 0, 0, channel] += 1
            with self.assertRaises(ValueError):
                assess_conversion(inputs, output)

    def test_all_rejected_or_false_inactive_cannot_pass(self):
        inputs, output = self.fixture()
        output[0, 0, :2] = 0
        output[0, 0, 2:, 3] = 1
        with self.assertRaises(ValueError):
            assess_conversion(inputs, output)
        inputs, output = self.fixture()
        output[0, 0, 2, 3] = 3
        with self.assertRaises(ValueError):
            assess_conversion(inputs, output)

    def test_nonfinite_or_excluded_radiance_is_rejected(self):
        for value in (np.nan, 1):
            inputs, output = self.fixture()
            output[0, 1, 0, 0] = value
            with self.assertRaises(ValueError):
                assess_conversion(inputs, output)
