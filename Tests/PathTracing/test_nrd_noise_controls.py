import copy
import unittest

import numpy as np

import test_nrd_constant_controls as fixtures
from run_nrd_noise_controls import signal, assess, SEEDS, AMPLITUDES, FRAMES, BASE


def readback(values, base):
    maximum = np.abs(values-base).max(axis=0)
    return dict(expectedRgb=base.tolist(), meanRgb=values.mean(axis=0).tolist(),
        maximumAbsoluteErrorRgb=maximum.tolist(), nonfiniteValues=0,
        passed=bool((maximum <= .004*np.maximum(1, base)).all()), rgbSamples=values.ravel().tolist())


class NoiseControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.constant = fixtures.ConstantControlTests().report()
        cls.report = {k: v for k, v in cls.constant.items() if k != 'checkpoints'}
        cls.report.update(mode='balanced-pair-noise', status='completed-noise-controls', seeds=list(SEEDS),
            amplitudes=list(AMPLITUDES), checkpoints=[])
        for seed in SEEDS:
            for amplitude in AMPLITUDES:
                for frame in FRAMES:
                    row = dict(case=3, seed=seed, amplitude=amplitude, elapsedFrames=frame,
                        sdkFrameIndex=frame-1, reset=frame == 1, dispatchCount=10)
                    for lobe, name in enumerate(('diffuse', 'specular')):
                        row[name] = readback(np.tile(BASE[lobe], (4096, 1)), BASE[lobe])
                        row['input'+name.capitalize()] = readback(signal(seed, amplitude, frame-1, lobe), BASE[lobe])
                        row['temporalNative'+name.capitalize()] = (
                            sum(signal(seed, amplitude, f, lobe).astype(float) for f in range(frame))/frame).ravel().tolist()
                    cls.report['checkpoints'].append(row)

    def test_balanced_nonnegative_reproducible_noise(self):
        a = signal(7, .75, 0, 0)
        self.assertTrue(np.array_equal(a.mean(axis=0), BASE[0]))
        self.assertTrue((a >= 0).all())
        self.assertTrue(np.array_equal(a[::2]+a[1::2], np.tile(BASE[0]*2, (2048, 1))))
        self.assertFalse(np.array_equal(a, signal(7, .75, 1, 0)))
        self.assertFalse(np.array_equal(a, signal(101, .75, 0, 0)))
        self.assertTrue(np.array_equal(a, signal(7, .75, 0, 0)))

    def test_complete_evidence_and_first_frame_is_descriptive(self):
        result = assess(self.report, self.constant)
        self.assertEqual(len(result), 54)
        self.assertEqual(sum(row['evaluated'] for row in result), 42)
        self.assertTrue(all(row['passed'] for row in result if row['evaluated']))

    def test_duplicate_missing_and_changed_runtime_are_rejected(self):
        for mutation in ('missing', 'duplicate', 'runtime', 'input', 'temporal'):
            report = copy.deepcopy(self.report)
            if mutation == 'missing':
                report['checkpoints'].pop()
            elif mutation == 'duplicate':
                report['checkpoints'][-1] = copy.deepcopy(report['checkpoints'][0])
            elif mutation == 'runtime':
                report['checkpoints'][0]['dispatchCount'] = 0
            elif mutation == 'input':
                row = report['checkpoints'][4]
                row['inputDiffuse'] = readback(signal(101, .25, 29, 0), BASE[0])
            else:
                report['checkpoints'][4]['temporalNativeDiffuse'][0] += .01
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                assess(report, self.constant)

    def test_numeric_quality_failure_is_not_execution_failure(self):
        report = copy.deepcopy(self.report)
        row = report['checkpoints'][4]
        row['diffuse'] = readback(np.tile(BASE[0]*1.2, (4096, 1)), BASE[0])
        result = assess(report, self.constant)
        failed = [r for r in result if r['evaluated'] and not r['passed']]
        self.assertEqual(len(failed), 1)
        self.assertGreater(failed[0]['output']['meanRgbBias'], .05)


if __name__ == '__main__':
    unittest.main()
