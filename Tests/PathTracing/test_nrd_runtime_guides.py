import unittest

import numpy as np

from validate_nrd_runtime_guides import NAMES, validate_guides


class RuntimeGuideTests(unittest.TestCase):
    def fixture(self):
        values = {name: ({}, np.zeros((1, 3, 1 if name == 'ViewZ' else 4))) for name in NAMES}
        values['NormalRoughness'][1][0, :2] = [0, 0, 1, .5]
        values['ViewZ'][1][0, :2, 0] = 2
        values['DenoiserPrimaryPosition'][1][0, :2, 3] = 1
        values['NrdConversionDiagnostics'][1][0, :, 0] = [0, 7, 4]
        values['NrdNormalRoughness'][1][0, :2] = [0, 0, 1, .5]
        values['NrdViewZ'][1][0, :, 0] = [2, 2, 1e6]
        values['NrdValidity'][1][0] = [[1, 1, 0, 0], [1, 0, 1, 0], [0, 0, 0, 0]]
        return values

    def test_valid(self):
        self.assertEqual(validate_guides(self.fixture()),
            dict(acceptedPixels=2, reconstructionPixels=1, excludedPixels=1))

    def test_corrupted_guides(self):
        for name, pixel, channel, value in (
                ('NrdValidity', 1, 3, 1), ('NrdValidity', 1, 1, 1),
                ('NrdViewZ', 0, 0, 3), ('NrdViewZ', 2, 0, 0),
                ('NrdNormalRoughness', 0, 3, .7), ('NrdNormalRoughness', 2, 0, .1),
                ('DenoiserPrimaryPosition', 0, 3, 0), ('NrdConversionDiagnostics', 0, 0, 11)):
            with self.subTest(name=name, pixel=pixel):
                values = self.fixture()
                values[name][1][0, pixel, channel] = value
                with self.assertRaises(ValueError):
                    validate_guides(values)

    def test_non_finite(self):
        for name in NAMES:
            values = self.fixture()
            values[name][1][0, 0, 0] = np.nan
            with self.assertRaises(ValueError):
                validate_guides(values)


if __name__ == '__main__':
    unittest.main()
