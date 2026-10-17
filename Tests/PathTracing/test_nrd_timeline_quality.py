import copy
import unittest

import numpy as np

from validate_nrd_disocclusion_quality import assess_region
from validate_nrd_timeline_quality import (CHECKPOINTS, RESOURCES, UNCERTAINTY_MULTIPLIER,
    validate_inventory, quality_verdict, assess_stability)


class TimelineQualityTests(unittest.TestCase):
    def region(self):
        reference = np.tile([.03, .7, .08], (100, 1))
        noisy = reference.copy()
        noisy[::2] *= 1.2
        noisy[1::2] *= .8
        return assess_region(reference, noisy, reference, reference)

    def cases(self):
        return [dict(case=case, regions={name: self.region() for name in
            (('retained',) if case == 'warm' else ('revealed', 'retained'))})
            for case, _, _, _ in CHECKPOINTS]

    def test_complete_timeline_passes(self):
        verdict, stability = quality_verdict(self.cases())
        self.assertEqual(verdict, 'passed-timeline-fixture')
        self.assertTrue(all(value['passed'] for value in stability.values()))
        self.assertEqual(UNCERTAINTY_MULTIPLIER, 1.0)

    def test_missing_duplicate_or_incomplete_quality_fails(self):
        for mutation in ('case', 'duplicate', 'region', 'number', 'checks', 'flag', 'nan'):
            cases = self.cases()
            if mutation == 'case':
                cases.pop()
            elif mutation == 'duplicate':
                cases[-1] = copy.deepcopy(cases[0])
            elif mutation == 'region':
                del cases[-1]['regions']['revealed']
            elif mutation == 'number':
                del cases[-1]['regions']['revealed']['nrdNrmse']
            elif mutation == 'checks':
                del cases[-1]['regions']['revealed']['checks']['meanRgbBias']
            elif mutation == 'flag':
                cases[-1]['regions']['revealed']['passed'] = False
            else:
                cases[-1]['regions']['revealed']['nativeNrmse'] = float('nan')
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                quality_verdict(cases)

    def test_bad_reference_is_inconclusive(self):
        cases = self.cases()
        region = cases[-1]['regions']['revealed']
        region['referencePairNrmse'] = .04
        region['checks']['referenceConverged'] = False
        region['passed'] = False
        self.assertEqual(quality_verdict(cases)[0], 'inconclusive-reference')

    def test_red_contamination_cannot_pass(self):
        cases = self.cases()
        region = cases[2]['regions']['revealed']
        region['meanRedExcess'] = .1
        region['checks']['meanRedExcess'] = False
        region['passed'] = False
        self.assertEqual(quality_verdict(cases)[0], 'failed-timeline-fixture')

    def test_settling_uncertainty_budget_and_region_identity(self):
        stop = {name: self.region() for name in ('revealed', 'retained')}
        for value in stop.values():
            value.update(referencePairNrmse=.01, nrdNrmse=.02)
        final = copy.deepcopy(stop)
        final['revealed']['nrdNrmse'] = .029
        self.assertTrue(assess_stability(stop, final)['revealed']['passed'])
        final['revealed']['nrdNrmse'] = .031
        self.assertFalse(assess_stability(stop, final)['revealed']['passed'])
        for field, value in (('sampledPixels', 99), ('referenceMeanRgb', [.1, .7, .08]),
                ('referencePairNrmse', .02)):
            other = copy.deepcopy(final)
            other['revealed'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                assess_stability(stop, other)

    def test_source_inventory_is_exact(self):
        report = dict(status='passed', failures=[],
            records=[dict(case=case, frame=frame, resource=resource)
                for case, frame, _, _ in CHECKPOINTS for resource in RESOURCES],
            cases=[dict(case=case, frame=frame) for case, frame, _, _ in CHECKPOINTS],
            runs=[dict(resource=resource) for resource in RESOURCES])
        validate_inventory(report)
        for field in ('records', 'cases', 'runs'):
            duplicate = copy.deepcopy(report)
            duplicate[field][-1] = copy.deepcopy(duplicate[field][0])
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_inventory(duplicate)
        wrong = copy.deepcopy(report)
        wrong['records'][0]['frame'] = 21
        with self.assertRaises(ValueError):
            validate_inventory(wrong)


if __name__ == '__main__':
    unittest.main()
