"""Fail-closed checks for the live conversion policy-rejection report."""
import unittest

import numpy as np

from validate_nrd_runtime_conversion import validate_policy_rejection


class RuntimePolicyTests(unittest.TestCase):
    def fixture(self):
        arrays = [np.zeros((1, 3, 4)) for _ in range(4)]
        arrays[0][0, :2, 3] = 1
        arrays[1][0, :, 0] = [6, 7, 4]
        arrays[2][0, 1] = 1
        arrays[3][0, 1] = 2
        return arrays

    def test_valid(self):
        self.assertEqual(validate_policy_rejection(*self.fixture()),
            dict(policyRejectedPixels=1, primaryPixels=2))

    def test_non_finite(self):
        for index in range(4):
            for value in (np.nan, np.inf, -np.inf):
                with self.subTest(index=index, value=value):
                    arrays = self.fixture()
                    arrays[index][0, 0, 0] = value
                    with self.assertRaises(ValueError):
                        validate_policy_rejection(*arrays)

    def test_invalid_markers_and_gates(self):
        for index, pixel, channel, value in ((0, 2, 3, 2), (1, 1, 0, 4),
                (1, 2, 0, 6), (1, 0, 0, 0), (1, 0, 0, 1), (1, 0, 3, 1)):
            with self.subTest(index=index, pixel=pixel, value=value):
                arrays = self.fixture()
                arrays[index][0, pixel, channel] = value
                with self.assertRaises(ValueError):
                    validate_policy_rejection(*arrays)

    def test_excluded_output_leaks(self):
        for index in (2, 3):
            for pixel in (0, 2):
                arrays = self.fixture()
                arrays[index][0, pixel, 0] = 0.01
                with self.assertRaises(ValueError):
                    validate_policy_rejection(*arrays)

    def test_shape_mismatch_and_empty(self):
        arrays = self.fixture()
        arrays[2] = np.zeros((1, 1, 4))
        with self.assertRaises(ValueError):
            validate_policy_rejection(*arrays)
        with self.assertRaises(ValueError):
            validate_policy_rejection(*[np.zeros((0, 1, 4)) for _ in range(4)])


if __name__ == '__main__':
    unittest.main()
