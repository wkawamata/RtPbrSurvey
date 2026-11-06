import unittest
import numpy as np
from validate_volume_roughness_texture import assess


class DirectionComparisonTests(unittest.TestCase):
    def test_equal(self):
        image = np.ones((32, 32, 4))
        self.assertEqual(assess(image, image)["maxError"], 0)

    def test_bad_evidence(self):
        image = np.ones((32, 32, 4))
        for other in (np.zeros_like(image), image * np.nan, image * 2):
            with self.assertRaises(ValueError):
                assess(image, other)
        with self.assertRaises(ValueError):
            assess(np.zeros_like(image), np.zeros_like(image))


if __name__ == "__main__":
    unittest.main()
