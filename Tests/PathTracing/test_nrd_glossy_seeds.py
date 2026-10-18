import copy
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from validate_nrd_glossy_seeds import (SEEDS, FRAMES, RESOURCES, EXPECTED,
    assess_roi, capture, check_capture, measure, summarize)


class GlossySeedTests(unittest.TestCase):
    def rows(self):
        reference = np.tile([1., 2, 3], (200, 1))
        native = reference.copy()
        native[::2] *= 1.2
        native[1::2] *= .8
        rows = []
        for seed, offset in zip(SEEDS, (.1, -.1, 0, .02, -.02)):
            result = assess_roi(reference*(1+offset), native, reference, reference)
            for frames in FRAMES:
                for name in EXPECTED:
                    rows.append(dict(copy.deepcopy(result), seed=seed, frames=frames, roi=name))
        return rows

    def test_signed_seed_variation_and_ensemble_bias_are_distinct(self):
        summaries = summarize(self.rows())
        self.assertEqual(len(summaries), 28)
        for row in summaries:
            self.assertEqual(row['biasFailedSeeds'], [7, 101])
            self.assertEqual(row['darkerSeeds'], [101, 1009])
            self.assertEqual(row['rmseFailedSeeds'], [])
            self.assertAlmostEqual(row['ensembleMeanRgbBias'], 0)
            self.assertAlmostEqual(row['maximumMeanRgbBias'], .1)
            self.assertFalse(row['allSeedsAllChannelsDarker'])
        self.assertFalse(set(SEEDS) & {11, 29})

    def test_summary_rejects_incomplete_duplicate_forged_or_changed_reference(self):
        rows = self.rows()
        for mutation in ('missing', 'duplicate', 'mean', 'nan', 'flag', 'reference', 'identity'):
            changed = copy.deepcopy(rows)
            if mutation == 'missing':
                changed.pop()
            elif mutation == 'duplicate':
                changed[-1] = changed[0]
            elif mutation == 'mean':
                changed[0]['nrdMeanRgb'] = [1, 2, 3]
            elif mutation == 'nan':
                changed[0]['nrdMeanRgb'][0] = float('nan')
            elif mutation == 'flag':
                changed[0]['passed'] = True
            elif mutation == 'reference':
                changed[0]['referencePairNrmse'] = .01
            else:
                changed[0]['seed'] = float(changed[0]['seed'])
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                summarize(changed)

    def test_measure_requires_full_valid_composition_and_fixed_masks(self):
        image = np.ones((12, 20, 4), dtype=np.float32)
        buffers = {name: image.copy() for name in RESOURCES}
        buffers['NrdCompositionDiagnostics'][:] = [1, 0, 0, 0]
        masks = {name: np.ones((12, 20), dtype=bool) for name in EXPECTED}
        rows = measure(buffers, [image, image], masks, 7, 30)
        self.assertEqual(len(rows), 14)
        self.assertTrue(all(row['passed'] for row in rows))
        buffers['NrdCompositionDiagnostics'][0, 0, 0] = 0
        with self.assertRaises(ValueError):
            measure(buffers, [image, image], masks, 7, 30)
        buffers['NrdCompositionDiagnostics'][:] = [1, 0, 0, 0]
        del masks[next(iter(masks))]
        with self.assertRaises(ValueError):
            measure(buffers, [image, image], masks, 7, 30)

    @patch('validate_nrd_glossy_seeds.assess_frame')
    def test_seed_checkpoint_camera_and_runtime_must_match(self, assess):
        target = dict(width=20, height=12, rayTMin=.001, rayTMax=100, normalBias=.001,
            lensShift=[0, 0], viewProjection=list(range(16)), previousViewProjection=list(range(16)),
            inverseViewProjection=list(range(16)))
        meta = dict(target, resource='PathTracing.NrdSceneColor', format=2, sampleStartIndex=120,
            primarySampling='pixel-center', samplesPerFrame=1, randomSeed=101,
            denoiserDistanceDiagnostics=True, nrdPreviewRequested=True,
            nrdPresentation='NRD RELAX diagnostic (per-pixel Native fallback)', nrdFailure='',
            nrdLastEvaluation=dict(frameIndex=120, recorded=True))
        runtime = dict(accumulatedSamples=120, targetSamples=0, samplesPerFrame=1, randomSeed=101,
            maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
            emissiveEnabled=False, russianRoulette=False, denoiserDistanceDiagnostics=True, nrdFailureCount=0)
        common = check_capture(meta, runtime, 'NrdSceneColor', 101, 120, target, None)
        check_capture(meta, runtime, 'NrdSceneColor', 101, 120, target, common)
        for field, value in (('randomSeed', 7), ('sampleStartIndex', 30), ('width', 21), ('format', 10),
                ('previousViewProjection', [0]*16), ('nrdLastEvaluation', dict(frameIndex=30, recorded=True))):
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_capture(dict(meta, **{field: value}), runtime, 'NrdSceneColor', 101, 120, target, None)
        with self.assertRaises(ValueError):
            check_capture(meta, dict(runtime, environmentSamplingMode=5), 'NrdSceneColor', 101, 120, target, None)
        with self.assertRaises(ValueError):
            check_capture(meta, runtime, 'NrdSceneColor', 101, 120, target, {})

    def test_capture_records_seed_and_checkpoint_and_cleans_up_failures(self):
        for outcome in ('success', 'exit', 'timeout'):
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as directory, \
                    patch('validate_nrd_glossy_seeds.subprocess.Popen') as start, \
                    patch('validate_nrd_glossy_seeds.sha', return_value='digest'), \
                    patch('validate_nrd_glossy_seeds.read_evidence', return_value=({}, np.ones((1, 1, 4)), {})), \
                    patch('validate_nrd_glossy_seeds.check_capture', return_value={'valid': True}):
                child = start.return_value
                child.wait.side_effect = [subprocess.TimeoutExpired('app', 180), 0] if outcome == 'timeout' else [0 if outcome == 'success' else 2]
                child.poll.return_value = None if outcome == 'timeout' else 0
                if outcome == 'success':
                    _, record, common = capture(Path(directory), 'SceneColor', 701, 120, {}, None)
                    self.assertEqual(record['seed'], 701)
                    self.assertEqual(common, {'valid': True})
                else:
                    with self.assertRaises(subprocess.TimeoutExpired if outcome == 'timeout' else ValueError):
                        capture(Path(directory), 'SceneColor', 701, 120, {}, None)
                command = start.call_args.args[0]
                self.assertEqual(command[command.index('-PathTracingSeed')+1], '701')
                self.assertEqual(command[command.index('-CaptureAfterFrames')+1], '120')
                self.assertEqual(command[-1], '-DisableVSync')
                self.assertEqual(child.kill.call_count, int(outcome == 'timeout'))


if __name__ == '__main__':
    unittest.main()
