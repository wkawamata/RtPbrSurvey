import copy
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch, MagicMock

import numpy as np

from refine_nrd_component_references import summarize, check_capture, capture, qualify, EXPECTED, CHANNELS, SAMPLES


class ComponentRefinementTests(unittest.TestCase):
    def rows(self):
        mask = np.ones((10, 10), dtype=bool)
        rows = []
        for component in CHANNELS:
            for roi in EXPECTED:
                image = np.ones((10, 10, 4), dtype=np.float32)
                if component == 'diffuse' and roi.startswith('metal-'):
                    image[..., :3] = 0
                rows.append(qualify(image, image, mask, component, roi))
        return rows

    def test_complete_inventory_keeps_inactive_separate(self):
        result = summarize(self.rows())
        diffuse = next(r for r in result if r['component'] == 'diffuse')
        self.assertEqual(diffuse, dict(component='diffuse', qualifiedPairs=7, inactiveZero=7, inconclusive=0))

    def test_incomplete_duplicate_or_missing_numeric_assessment_is_rejected(self):
        rows = self.rows()
        for changed in (rows[:-1], rows[:-1]+[rows[0]], [dict(r, status='unknown') for r in rows]):
            with self.assertRaises(ValueError):
                summarize(changed)
        changed = copy.deepcopy(rows)
        changed[0]['referencePairNrmse'] = .1
        with self.assertRaises(ValueError):
            summarize(changed)
        changed = copy.deepcopy(rows)
        del changed[0]['referencePairNrmse']
        with self.assertRaises(ValueError):
            summarize(changed)

    def test_large_pair_difference_stays_inconclusive(self):
        rows = self.rows()
        changed = next(r for r in rows if r['component'] == 'specular')
        changed.update(status='inconclusive-reference', qualified=False, referencePairNrmse=.06)
        self.assertEqual(next(r for r in summarize(rows) if r['component'] == 'specular')['inconclusive'], 1)

    def test_wrong_capture_state_is_rejected(self):
        keys = ('width', 'height', 'viewProjection', 'previousViewProjection', 'inverseViewProjection',
            'rayTMin', 'rayTMax', 'normalBias', 'lensShift')
        target = {key: 1 for key in keys}
        meta = dict(target, resource='PathTracing.SceneColor', format=10, sampleStartIndex=SAMPLES,
            samplesPerFrame=1, randomSeed=11, primarySampling='pixel-center', nrdPreviewRequested=False,
            nrdPresentation='Native PT', denoiserDistanceDiagnostics=False, pathTracingReferenceComponent='diffuse')
        runtime = dict(accumulatedSamples=SAMPLES, samplesPerFrame=1, randomSeed=11, targetSamples=SAMPLES,
            maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
            emissiveEnabled=False, russianRoulette=False, nrdPreviewRequested=False, nrdFailureCount=0)
        check_capture(meta, runtime, 'diffuse', 11, target)
        for key, value in (('samplesPerFrame', 16), ('sampleStartIndex', 8192), ('randomSeed', 29),
                ('nrdPreviewRequested', True), ('width', 2)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_capture(dict(meta, **{key: value}), runtime, 'diffuse', 11, target)
        with self.assertRaises(ValueError):
            check_capture(meta, dict(runtime, accumulatedSamples=8192), 'diffuse', 11, target)

    def test_normal_process_exit_without_evidence_is_not_success(self):
        child = MagicMock()
        child.wait.return_value = 0
        child.poll.return_value = 0
        with patch('refine_nrd_component_references.subprocess.Popen', return_value=child), \
                patch('refine_nrd_component_references.Path.is_file', return_value=False), patch('builtins.print'):
            with self.assertRaisesRegex(ValueError, 'without complete capture/log evidence'):
                capture(Path('app.exe'), Path('evidence'), 'diffuse', 11, {})
        child.kill.assert_not_called()

    def test_timeout_reaps_owned_capture_process(self):
        child = MagicMock()
        child.wait.side_effect = [subprocess.TimeoutExpired('capture', 3000), 0]
        child.poll.return_value = None
        with patch('refine_nrd_component_references.subprocess.Popen', return_value=child), patch('builtins.print'):
            with self.assertRaises(subprocess.TimeoutExpired):
                capture(Path('app.exe'), Path('evidence'), 'diffuse', 11, {})
        child.kill.assert_called_once()
        self.assertEqual(child.wait.call_count, 2)

    def test_hidden_capture_only_changes_process_startup(self):
        child = MagicMock()
        child.wait.return_value = 0
        child.poll.return_value = 0
        with patch('refine_nrd_component_references.subprocess.Popen', return_value=child) as start, \
                patch('refine_nrd_component_references.Path.is_file', return_value=False), patch('builtins.print'):
            with self.assertRaises(ValueError):
                capture(Path('app.exe'), Path('evidence'), 'diffuse', 11, {}, hidden=True)
        startup = start.call_args.kwargs['startupinfo']
        self.assertEqual(startup.wShowWindow, 0)
        self.assertTrue(startup.dwFlags & subprocess.STARTF_USESHOWWINDOW)
        self.assertNotIn('--hidden', start.call_args.args[0])


if __name__ == '__main__':
    unittest.main()
