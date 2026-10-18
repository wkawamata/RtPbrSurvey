import copy
import unittest

import numpy as np

from validate_nrd_disocclusion_quality import (CASES, RESOURCES, SAMPLES, LIMITS,
    assess_region, validate_inventory, check_reference)


class DisocclusionQualityTests(unittest.TestCase):
    def values(self):
        reference = np.tile([.03, .7, .08], (100, 1))
        native = reference.copy()
        native[::2] *= 1.2
        native[1::2] *= .8
        return reference.copy(), native, reference.copy(), reference.copy()

    def test_correct_denoising_passes_fixed_limits(self):
        result = assess_region(*self.values())
        self.assertTrue(result['passed'])
        self.assertEqual(result['nrdNrmse'], 0)
        self.assertEqual(LIMITS, dict(referencePairNrmse=.03, meanRgbBias=.05,
            meanRedExcess=.02, p95RedExcess=.05, nrdToNativeRmse=1.0))

    def test_unconverged_reference_and_rmse_regression(self):
        values = self.values()
        values[2][:] *= 1.2
        self.assertFalse(assess_region(*values)['checks']['referenceConverged'])
        values = self.values()
        values[0][:] *= 1.5
        result = assess_region(*values)
        self.assertFalse(result['checks']['noRmseRegression'])
        self.assertFalse(result['checks']['meanRgbBias'])

    def test_red_ghost_is_rejected(self):
        values = self.values()
        values[0][:, 0] += .1
        result = assess_region(*values)
        self.assertFalse(result['checks']['meanRedExcess'])
        self.assertFalse(result['checks']['p95RedExcess'])
        self.assertFalse(result['passed'])

    def test_malformed_black_and_nonfinite_regions(self):
        for mode in ('short', 'shape', 'black', 'negative', 'nan'):
            values = list(self.values())
            if mode == 'short':
                values = [value[:19] for value in values]
            elif mode == 'shape':
                values[0] = values[0][:-1]
            elif mode == 'black':
                values[2][:] = 0
                values[3][:] = 0
            else:
                values[0][0, 0] = -1 if mode == 'negative' else float('nan')
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                assess_region(*values)

    def inventory(self):
        return dict(status='passed', failures=[], records=[dict(name=case+'-'+resource)
            for case, _, _ in CASES for resource in RESOURCES], cases=[dict(case=case) for case, _, _ in CASES])

    def test_source_inventory_rejects_duplicates_missing_and_wrong_cases(self):
        validate_inventory(self.inventory())
        for field in ('records', 'cases'):
            report = self.inventory()
            report[field][-1] = copy.deepcopy(report[field][0])
            with self.assertRaises(ValueError):
                validate_inventory(report)
            report = self.inventory()
            report[field].pop()
            with self.assertRaises(ValueError):
                validate_inventory(report)
        report = self.inventory()
        report['status'] = 'failed'
        with self.assertRaises(ValueError):
            validate_inventory(report)

    def reference(self):
        meta = dict(resource='PathTracing.SceneColor', format=10, width=64, height=64,
            samplesPerFrame=1, sampleStartIndex=SAMPLES, randomSeed=11, primarySampling='pixel-center',
            nrdPreviewRequested=False, nrdPresentation='Native PT', lensShift=[0, 0],
            rayTMin=.001, rayTMax=1000, normalBias=.001,
            viewProjection=np.eye(4).tolist(), inverseViewProjection=np.eye(4).tolist())
        diagnostics = dict(accumulatedSamples=SAMPLES, targetSamples=SAMPLES, randomSeed=11,
            samplesPerFrame=1, maxBounces=2, environmentEnabled=True, environmentSamplingMode=5,
            directLightingEnabled=False, emissiveEnabled=False, russianRoulette=False)
        return meta, diagnostics

    def test_reference_requires_camera_samples_and_transport(self):
        target, diagnostics = self.reference()
        check_reference(target, diagnostics, target, 11)
        for key, value in (('randomSeed', 29), ('sampleStartIndex', 0),
                ('nrdPreviewRequested', True), ('samplesPerFrame', 16), ('format', 2)):
            other = copy.deepcopy(target)
            other[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_reference(other, diagnostics, target, 11)
        for key in ('accumulatedSamples', 'targetSamples', 'environmentSamplingMode', 'maxBounces'):
            other = dict(diagnostics)
            other[key] = 0
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_reference(target, other, target, 11)
        other = copy.deepcopy(target)
        other['viewProjection'][0][0] = 1.01
        with self.assertRaises(ValueError):
            check_reference(other, diagnostics, target, 11)

    def test_environment_map_reference_is_explicit_not_default(self):
        target, diagnostics = self.reference()
        diagnostics['environmentSamplingMode'] = 7
        with self.assertRaises(ValueError):
            check_reference(target, diagnostics, target, 11)
        check_reference(target, diagnostics, target, 11, environment_mode=7)
        diagnostics['environmentSamplingMode'] = 5
        with self.assertRaises(ValueError):
            check_reference(target, diagnostics, target, 11, environment_mode=7)

    def test_sample_count_and_depth_must_match_explicit_contract(self):
        target, diagnostics = self.reference()
        target['sampleStartIndex'] = 4096
        diagnostics.update(accumulatedSamples=4096, targetSamples=4096, maxBounces=4)
        check_reference(target, diagnostics, target, 11, sample_count=4096, max_bounces=4)
        with self.assertRaises(ValueError):
            check_reference(target, diagnostics, target, 11)
        for key, value in (('targetSamples', 1024), ('accumulatedSamples', 1024), ('maxBounces', 8)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_reference(target, dict(diagnostics, **{key: value}), target, 11,
                    sample_count=4096, max_bounces=4)


if __name__ == '__main__':
    unittest.main()
