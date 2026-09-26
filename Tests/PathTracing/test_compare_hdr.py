import math
from pathlib import Path
import struct
import tempfile
import unittest

from compare_hdr import mean_image, read_pfm, rmse


class HdrTests(unittest.TestCase):
    def test_orientation_roi_and_hdr_range(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.pfm"
            path.write_bytes(b"PF\n2 2\n-1.0\n" + struct.pack("<12f", *range(12)))
            dimensions, data = read_pfm(path, (1, 0, 1, 2))
            self.assertEqual(dimensions, (2, 2))
            self.assertEqual(list(data), [9, 10, 11, 3, 4, 5])
            with self.assertRaises(ValueError):
                read_pfm(path, (2, 0, 1, 1))

    def test_nonfinite_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.pfm"
            path.write_bytes(b"PF\n1 1\n-1.0\n" + struct.pack("<3f", 1, math.nan, 3))
            with self.assertRaises(ValueError):
                read_pfm(path, (0, 0, 1, 1))

    def test_metrics(self):
        self.assertEqual(mean_image([[1, 3, 5], [3, 5, 7]]), [2, 4, 6])
        self.assertEqual(rmse([1, 2, 3], [3, 4, 5]), 2)
        self.assertEqual(rmse([1, 2, 3], [1, 2, 3]), 0)
        with self.assertRaises(ValueError):
            rmse([1], [1, 2])


if __name__ == "__main__":
    unittest.main()
