import unittest

from validate_transport import hemisphere_integral, paired_agreement, room_fixture


class TransportValidationTests(unittest.TestCase):
    def test_equal_means_pass(self):
        self.assertEqual(paired_agreement([1, 1, 1, 1], [1, 1, 1, 1])["status"], "passed")

    def test_systematic_difference_fails(self):
        self.assertEqual(paired_agreement([1, 1, 1, 1], [1.1]*4)["status"], "failed")

    def test_high_uncertainty_is_not_passed(self):
        self.assertEqual(paired_agreement([1]*4, [.5, 1.5, .5, 1.5])["status"], "inconclusive")

    def test_invalid_observations_rejected(self):
        for left, right in (([1]*3, [1]*3), ([0]*4, [0]*4), ([1]*4, [float("nan")]*4)):
            with self.assertRaises(ValueError):
                paired_agreement(left, right)

    def test_room_has_six_closed_walls_and_deep_paths(self):
        scene, preset = room_fixture()
        self.assertEqual(len(scene["nodes"]), 6)
        self.assertTrue(all(n["primitive"]["kind"] == "cube" for n in scene["nodes"]))
        self.assertEqual(preset["pathTracing"]["maxBounces"], 8)
        self.assertFalse(preset["pathTracing"]["russianRouletteEnabled"])

    def test_quadrature_is_finite_and_converges(self):
        low = hemisphere_integral(.5, .5, 1, .18, 128)
        high = hemisphere_integral(.5, .5, 1, .18, 256)
        self.assertTrue(0 < high < 1)
        self.assertLess(abs(low/high-1), .002)

    def test_rough_single_scattering_metal_loses_energy(self):
        self.assertLess(hemisphere_integral(1, .5, 1, 1), .5)


if __name__ == "__main__":
    unittest.main()
