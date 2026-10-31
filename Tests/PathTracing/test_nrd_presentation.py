import unittest

import numpy as np

from validate_nrd_presentation import assess_pair


class PresentationTests(unittest.TestCase):
    def test_qualified_must_change_visible_pixels(self):
        native = np.zeros((100, 100, 3), dtype=np.uint8)
        with self.assertRaises(ValueError):
            assess_pair(native, native, True)
        preview = native.copy()
        preview[25:75, 40:65] = 1
        self.assertGreaterEqual(assess_pair(native, preview, True)['changedPixels'], 100)

    def test_fallback_must_be_exact(self):
        native = np.zeros((100, 100, 3), dtype=np.uint8)
        self.assertEqual(assess_pair(native, native, False)['changedPixels'], 0)
        preview = native.copy()
        preview[40, 50, 0] = 1
        with self.assertRaises(ValueError):
            assess_pair(native, preview, False)

    def test_ui_is_excluded_and_bad_shape_rejected(self):
        native = np.zeros((100, 100, 3), dtype=np.uint8)
        preview = native.copy()
        preview[:, :30] = 1
        self.assertEqual(assess_pair(native, preview, False)['changedPixels'], 0)
        with self.assertRaises(ValueError):
            assess_pair(native, preview[:50], False)


if __name__ == '__main__':
    unittest.main()
