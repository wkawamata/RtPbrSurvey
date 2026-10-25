import unittest

import numpy as np

from validate_thin_transmission import compare_rgb


class ThinComparisonTests(unittest.TestCase):
    def test_exact_and_tinted(self):
        base = np.full((2, 3, 3), 0.5)
        tint = np.array([0.2, 0.4, 0.8])
        self.assertEqual(compare_rgb(base*tint, base*tint, 0), 0)
        with self.assertRaises(ValueError):
            compare_rgb(base, base*tint)

    def test_shapes_and_finiteness(self):
        base = np.zeros((2, 3, 3))
        for invalid in (np.zeros((3, 2, 3)), np.zeros((2, 3, 2)), np.full((2, 3, 3), np.nan)):
            with self.assertRaises(ValueError):
                compare_rgb(invalid, base)
        with self.assertRaises(ValueError):
            compare_rgb(base, np.full_like(base, np.inf))

    def test_fixed_tolerance(self):
        base = np.zeros((2, 3, 3))
        self.assertLessEqual(compare_rgb(base+0.0009, base), 0.001)
        with self.assertRaises(ValueError):
            compare_rgb(base+0.0011, base)


if __name__ == '__main__':
    unittest.main()
