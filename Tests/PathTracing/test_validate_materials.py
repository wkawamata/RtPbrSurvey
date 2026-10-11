import unittest
import numpy as np
from validate_materials import expected_material, analyze


class MaterialTests(unittest.TestCase):
    def test_factors_change_shared_texture(self):
        self.assertGreater(np.linalg.norm(expected_material(0, 'Albedo')-expected_material(1, 'Albedo')), .1)

    def test_factor_only_emission(self):
        np.testing.assert_allclose(expected_material(0, 'Emissive', True), [.1, .3, .9])

    def test_factor_only_albedo(self):
        np.testing.assert_allclose(expected_material(0, 'Albedo', True), [.2, .4, .6, 1])

    def test_normal_scale_changes_direction(self):
        self.assertGreater(np.linalg.norm(expected_material(0, 'NormalRoughness')-expected_material(1, 'NormalRoughness')), .4)

    def test_wrong_material_fails(self):
        values = np.tile(expected_material(0, 'Albedo'), (40, 100, 1))
        checks = analyze(values, np.ones((40, 100), dtype=bool), 'Albedo')
        self.assertTrue(checks[0]['passed'])
        self.assertFalse(checks[1]['passed'])


if __name__ == '__main__':
    unittest.main()
