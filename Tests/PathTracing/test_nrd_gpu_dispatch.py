import unittest

import numpy as np

from validate_nrd_gpu_dispatch import assess_output


class GpuDispatchTests(unittest.TestCase):
    def setUp(self):
        self.source = np.ones((16, 16, 4), dtype=np.float32)
        self.output = self.source * 0.5
        self.admission = np.zeros_like(self.source)
        self.admission[:12, :, 0] = 1
        self.output[12:] = 0

    def test_valid_output(self):
        self.assertEqual(assess_output(self.source, self.output, self.admission)["changedPixels"], 192)

    def test_nonfinite_and_negative(self):
        for invalid in (np.nan, np.inf, -1.0):
            changed = self.output.copy()
            changed[0, 0, 0] = invalid
            with self.assertRaises(ValueError):
                assess_output(self.source, changed, self.admission)

    def test_excluded_pixels(self):
        self.output[15, 0, 0] = 1
        with self.assertRaises(ValueError):
            assess_output(self.source, self.output, self.admission)

    def test_passthrough_and_missing_signal(self):
        for changed in (self.source * (self.admission[..., :1] == 1), np.zeros_like(self.output)):
            with self.assertRaises(ValueError):
                assess_output(self.source, changed, self.admission)

    def test_wrong_shape(self):
        with self.assertRaises(ValueError):
            assess_output(self.source, self.output[:-1], self.admission)


if __name__ == "__main__":
    unittest.main()
