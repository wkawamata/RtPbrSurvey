import unittest
import numpy as np
from validate_volume_furnace import assess_status


class StatusTests(unittest.TestCase):
    def test_counts(self):
        image = np.zeros((128, 128, 4))
        image[48:64, 48:80, 0] = 3
        image[64:80, 48:64, 0] = 7
        self.assertEqual(assess_status(image), dict(normal=256, budgetTerminated=512,
                                                  roughRejected=256, total=1024))

    def test_errors_rejected(self):
        for code in (1, 2, 4, 5, 6, 8, .5, np.nan):
            image = np.zeros((128, 128, 4))
            image[64, 64, 0] = code
            with self.subTest(code=code), self.assertRaises(ValueError):
                assess_status(image)


if __name__ == "__main__":
    unittest.main()
