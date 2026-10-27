import unittest

import numpy as np

from qualify_nrd_component_references import qualify, grouping, EXPECTED


class ComponentQualificationTests(unittest.TestCase):
    def image(self, value=1):
        image = np.ones((10, 10, 4), dtype=np.float32)
        image[..., :3] = value
        return image

    def test_independent_reference_disagreement_is_inconclusive(self):
        roi = next(name for name in EXPECTED if name.startswith('dielectric-'))
        mask = np.ones((10, 10), dtype=bool)
        self.assertTrue(qualify(self.image(), self.image(), mask, 'diffuse', roi)['qualified'])
        result = qualify(self.image(), self.image(1.1), mask, 'diffuse', roi)
        self.assertFalse(result['qualified'])
        self.assertEqual(result['status'], 'inconclusive-reference')
        self.assertGreater(result['referencePairNrmse'], .03)

    def test_zero_metal_diffuse_is_explicitly_inactive_not_divided_by_zero(self):
        roi = next(name for name in EXPECTED if name.startswith('metal-'))
        mask = np.ones((10, 10), dtype=bool)
        result = qualify(self.image(0), self.image(0), mask, 'diffuse', roi)
        self.assertEqual(result['status'], 'exact-zero-inactive')
        self.assertIsNone(result['referencePairNrmse'])
        with self.assertRaises(ValueError):
            qualify(self.image(.01), self.image(.01), mask, 'diffuse', roi)

    def test_dark_or_invalid_references_are_not_silently_qualified(self):
        roi = next(iter(EXPECTED))
        mask = np.ones((10, 10), dtype=bool)
        result = qualify(self.image(0), self.image(0), mask, 'total', roi)
        self.assertEqual(result['status'], 'inconclusive-dark')
        for image in (self.image(-1), self.image(np.nan)):
            with self.assertRaises(ValueError):
                qualify(image, self.image(), mask, 'total', roi)

    def test_sample_grouping_uses_fixed_numerical_budget(self):
        a = self.image()
        self.assertEqual(grouping(a, a)['maximumAbsoluteDifference'], 0)
        self.assertLessEqual(grouping(a, self.image(float(np.nextafter(np.float16(1), np.float16(2)))))['maximumBoundRatio'], 1)
        with self.assertRaises(ValueError):
            grouping(a, self.image(1.1))

    def test_component_and_roi_identity_are_required(self):
        mask = np.ones((10, 10), dtype=bool)
        roi = next(iter(EXPECTED))
        for component, name in (('unknown', roi), ('total', 'unknown')):
            with self.subTest(component=component, roi=name), self.assertRaises(ValueError):
                qualify(self.image(), self.image(), mask, component, name)

    def test_incomplete_or_malformed_roi_is_rejected(self):
        roi = next(iter(EXPECTED))
        mask = np.ones((10, 10), dtype=bool)
        sparse = mask.copy()
        sparse[0, 0] = False
        for invalid in (sparse, mask.astype(int), mask[:9]):
            with self.subTest(shape=invalid.shape, dtype=invalid.dtype), self.assertRaises(ValueError):
                qualify(self.image(), self.image(), invalid, 'total', roi)
        with self.assertRaises(ValueError):
            qualify(self.image(), self.image()[:9], mask, 'total', roi)


if __name__ == '__main__':
    unittest.main()
