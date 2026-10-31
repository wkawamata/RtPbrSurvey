import copy
import unittest

from run_nrd_constant_controls import assess


class ConstantControlTests(unittest.TestCase):
    def report(self):
        report = dict(schemaVersion=1, width=64, height=64, sdkVersion='4.17.3', prepass=[30, 50],
            normalEncoding=4, roughnessEncoding=1, d3d12Errors=0, failures=[],
            status='passed-constant-signal-controls', checkpoints=[])
        for case in range(4):
            for frames in (1, 30, 120):
                row = dict(case=case, elapsedFrames=frames, sdkFrameIndex=frames-1,
                    reset=frames == 1, dispatchCount=10)
                for lobe in ('diffuse', 'specular'):
                    rgb = [0., 0, 0]
                    if lobe == 'diffuse' and case in (1, 3):
                        rgb = [.125, .5, 2]
                    if lobe == 'specular' and case in (2, 3):
                        rgb = [2, .25, .125]
                    row[lobe] = dict(expectedRgb=rgb, meanRgb=rgb, maximumAbsoluteErrorRgb=[0, 0, 0],
                        nonfiniteValues=0, passed=True, rgbSamples=rgb*4096)
                report['checkpoints'].append(row)
        return report

    def test_complete_known_controls(self):
        results = assess(self.report())
        self.assertEqual(len(results), 24)
        self.assertTrue(all(row['passed'] for row in results))

    def test_rejects_missing_duplicate_or_unexecuted_checkpoint(self):
        for mutation in ('missing', 'duplicate', 'reset', 'dispatch'):
            report = self.report()
            if mutation == 'missing':
                report['checkpoints'].pop()
            elif mutation == 'duplicate':
                report['checkpoints'][-1] = copy.deepcopy(report['checkpoints'][0])
            elif mutation == 'reset':
                report['checkpoints'][1]['reset'] = True
            else:
                report['checkpoints'][0]['dispatchCount'] = 0
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                assess(report)

    def test_rejects_missing_or_forged_numeric_evidence(self):
        for mutation in ('raw', 'sample', 'mean', 'maximum', 'expected', 'nan', 'flag'):
            report = self.report()
            row = report['checkpoints'][0]['diffuse']
            if mutation == 'raw':
                row.pop('rgbSamples')
            elif mutation == 'sample':
                row['rgbSamples'][0] = .1
            elif mutation == 'mean':
                row['meanRgb'] = [1, 0, 0]
            elif mutation == 'maximum':
                row['maximumAbsoluteErrorRgb'] = [1, 0, 0]
            elif mutation == 'expected':
                row['expectedRgb'] = [1, 0, 0]
            elif mutation == 'nan':
                row['rgbSamples'][0] = float('nan')
            else:
                row['passed'] = False
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                assess(report)


if __name__ == '__main__':
    unittest.main()
