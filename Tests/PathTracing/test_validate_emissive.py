import unittest
import numpy as np
from validate_emissive import rectangle_integral, reference, fixture
from pathlib import Path
import tempfile


class EmissiveReferenceTests(unittest.TestCase):
    def test_quadrature_converges_and_channels_follow_emission(self):
        low = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 32)
        high = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 64)
        np.testing.assert_allclose(low, high, rtol=1e-10)
        np.testing.assert_allclose(high/high[2], [4, 2, 1])
        self.assertGreater(high.min(), 0)

    def test_increased_distance_reduces_radiance(self):
        close = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8)
        far = rectangle_integral([0, -3, 0], [0, 1, 0], .5, .8)
        self.assertTrue(np.all(far < close))

    def test_fixture_reference_is_finite(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, preset = fixture(Path(directory))
            value = reference(scene)
            self.assertGreater(value['mean'], 0)
            self.assertLess(value['quadratureRelativeChange'], .002)
            self.assertFalse(preset['pathTracing']['environmentEnabled'])


if __name__ == '__main__':
    unittest.main()
