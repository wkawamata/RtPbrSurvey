import copy
import unittest

import numpy as np

from compare_nrd_component_references import assess, summarize, qualify, joint_assessment, EXPECTED, CHANNELS, PROFILES, FRAMES


class ComponentComparisonTests(unittest.TestCase):
    def image(self, value):
        image = np.ones((10, 10, 4), dtype=np.float32)
        image[..., :3] = value
        return image

    def assessment(self, output=1, first=1, second=1, component='total', roi=None):
        roi = roi or next(name for name in EXPECTED if name.startswith('dielectric-'))
        a, b = self.image(first), self.image(second)
        mask = np.ones((10, 10), dtype=bool)
        return assess(self.image(output)[..., :3], a, b, mask, component, roi,
            qualify(a, b, mask, component, roi))

    def rows(self):
        return [dict(self.assessment(*(0, 0, 0) if c == 'diffuse' and r.startswith('metal-') else (1, 1, 1),
            component=c, roi=r), profile=p, frames=f)
            for p in PROFILES for f in FRAMES for c in CHANNELS for r in EXPECTED]

    def test_bias_is_measured_only_with_qualified_reference(self):
        self.assertTrue(self.assessment()['passed'])
        row = self.assessment(output=.8)
        self.assertFalse(row['passed'])
        self.assertAlmostEqual(row['meanRgbBias'], .2, places=6)
        self.assertTrue(all(x < 0 for x in row['signedMeanRgbError']))
        self.assertFalse(row['nativeLobeRmseComparisonAvailable'])

    def test_unqualified_reference_cannot_pass_or_produce_bias_metrics(self):
        row = self.assessment(output=1, first=1, second=1.2)
        self.assertIsNone(row['passed'])
        self.assertEqual(row['status'], 'excluded-unqualified-reference')
        self.assertNotIn('meanRgbBias', row)
        a, mask = self.image(1), np.ones((10, 10), dtype=bool)
        roi = next(iter(EXPECTED))
        forged = qualify(a, a, mask, 'total', roi)
        forged['qualified'] = False
        with self.assertRaises(ValueError):
            assess(a[..., :3], a, a, mask, 'total', roi, forged)

    def test_zero_metal_diffuse_and_energy_leak_are_distinct(self):
        roi = next(name for name in EXPECTED if name.startswith('metal-'))
        self.assertEqual(self.assessment(0, 0, 0, 'diffuse', roi)['status'], 'inactive-zero-verified')
        row = self.assessment(.001, 0, 0, 'diffuse', roi)
        self.assertFalse(row['passed'])
        self.assertEqual(row['status'], 'inactive-energy-leak')

    def test_bad_output_is_rejected(self):
        for value in (-1, np.nan, np.inf):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.assessment(output=value)

    def test_summary_rejects_duplicates_missing_cases_and_missing_metrics(self):
        rows = self.rows()
        self.assertEqual(len(summarize(rows)), 12)
        for changed in (rows[:-1], rows[:-1]+[rows[0]],
                [dict(row, meanRgbBias=.2) for row in rows],
                [dict(row, status='unknown') for row in rows]):
            if changed == rows:
                continue
            with self.assertRaises(ValueError):
                summarize(changed)
        changed = copy.deepcopy(rows)
        del changed[0]['outputNrmse']
        with self.assertRaises(ValueError):
            summarize(changed)
        changed = copy.deepcopy(rows)
        changed[0]['passed'] = False
        with self.assertRaises(ValueError):
            summarize(changed)
        with self.assertRaises(ValueError):
            summarize([{key: row[key] for key in ('profile', 'frames', 'component', 'roi', 'pixels')} for row in rows])

    def test_joint_description_detects_cancellation_without_quality_promotion(self):
        for specular, total, expected in ((.9, 2, 0), (1.1, 2.2, 1)):
            rows = [self.assessment(1.1, component='diffuse'), self.assessment(specular, component='specular'),
                self.assessment(total, 2, 2)]
            result = joint_assessment(rows)
            self.assertAlmostEqual(result['lobeErrorCancellationRatio'], expected, places=6)
            self.assertEqual(result['qualityVerdict'], 'descriptive-only')
            self.assertAlmostEqual(result['diffuseReferenceLuminanceFraction'], .5)
            self.assertAlmostEqual(result['specularReferenceLuminanceFraction'], .5)

    def test_joint_description_excludes_unqualified_component_and_duplicate(self):
        rows = [self.assessment(component=c) for c in CHANNELS]
        rows[1] = self.assessment(first=1, second=1.2, component='diffuse')
        result = joint_assessment(rows)
        self.assertEqual(result['qualityVerdict'], 'not-assessed')
        self.assertNotIn('lobeErrorCancellationRatio', result)
        with self.assertRaises(ValueError):
            joint_assessment([rows[0]]*3)


if __name__ == '__main__':
    unittest.main()
