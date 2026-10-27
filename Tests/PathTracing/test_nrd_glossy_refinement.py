import copy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image

from refine_nrd_glossy_rois import (EXPECTED, ROI_LIMITS, SELECTION, SAMPLES,
    capture_reference, evaluate, verify_masks)
from validate_part1 import sha


class GlossyRefinementTests(unittest.TestCase):
    def test_fresh_capture_fixes_depth_samples_transport_and_preserves_inputs(self):
        scene, preset = dict(renderPreset='source.json'), dict(pathTracing=dict(nrdPreview=True, maxBounces=8))
        before = copy.deepcopy((scene, preset))
        with tempfile.TemporaryDirectory() as directory, \
                patch('refine_nrd_glossy_rois.subprocess.Popen') as start, \
                patch('refine_nrd_glossy_rois.sha', return_value='digest'), \
                patch('refine_nrd_glossy_rois.read_evidence', return_value=({}, np.zeros((1, 1, 4)), {})), \
                patch('refine_nrd_glossy_rois.check_reference') as check:
            child = start.return_value
            child.wait.return_value = child.poll.return_value = 0
            _, record = capture_reference(Path(directory), scene, preset, {}, 29)
            command = record['command']
            self.assertEqual(command[command.index('-PathTracingSamples')+1], '32768')
            self.assertEqual(command[-1], '-DisableVSync')
            saved = json.loads((Path(directory)/'reference-b4-29-preset.json').read_text())
            self.assertEqual(saved['pathTracing']['maxBounces'], 4)
            self.assertFalse(saved['pathTracing']['nrdPreview'])
            self.assertFalse(saved['pathTracing']['denoiserDistanceDiagnostics'])
            self.assertEqual(check.call_args.kwargs, dict(environment_mode=7, sample_count=SAMPLES, max_bounces=4))
            self.assertEqual((scene, preset), before)
            child.kill.assert_not_called()

    def test_timeout_and_exit_failure_do_not_accept_capture(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as directory, \
                    patch('refine_nrd_glossy_rois.subprocess.Popen') as start:
                child = start.return_value
                child.wait.side_effect = [subprocess.TimeoutExpired('app', 3000), 0] if timeout else [2]
                child.poll.return_value = None if timeout else 2
                with self.assertRaises(subprocess.TimeoutExpired if timeout else ValueError):
                    capture_reference(Path(directory), {}, dict(pathTracing={}), {}, 11)
                self.assertEqual(child.kill.call_count, int(timeout))

    def test_fixed_masks_and_measurements_reject_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            image = np.ones((20, 20, 4), dtype=np.float32)
            buffers = dict(SceneColor=image, NrdSceneColor=image)
            references = [image, image]
            regions = {name: np.ones((20, 20), dtype=bool) for name in EXPECTED}
            rows = evaluate(buffers, references, regions)
            for row in rows:
                name = row['roi'].replace('/', '__')+'.png'
                Image.fromarray(np.full((20, 20), 255, dtype=np.uint8)).save(folder/name)
                row.update(maskFile=name, maskSha256=sha(folder/name), logicalMaskSha256=
                    hashlib.sha256(regions[row['roi']].astype(np.uint8).tobytes()).hexdigest())
            report = dict(schemaVersion=1, status='passed-static-rois', failures=[], rois=rows,
                selection=SELECTION, limits=ROI_LIMITS, sourceRecordsRevalidated=16,
                sourceEvidenceReexecuted=False, freshGpuCaptures=0, subjectiveVerdict='unanswered',
                fullIntegrationReady=False)
            verify_masks(folder, report, buffers, references, regions)
            for mutation in ('duplicate', 'mask', 'measurement', 'status', 'selection', 'rerun'):
                changed = copy.deepcopy(report)
                if mutation == 'duplicate':
                    changed['rois'][-1] = changed['rois'][0]
                elif mutation == 'mask':
                    changed['rois'][0]['logicalMaskSha256'] = 'changed'
                elif mutation == 'measurement':
                    changed['rois'][0]['nativeNrmse'] = .2
                elif mutation == 'status':
                    changed['status'] = 'inconclusive-rois'
                elif mutation == 'selection':
                    changed['selection']['darkQuantile'] = .3
                else:
                    changed['sourceEvidenceReexecuted'] = True
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    verify_masks(folder, changed, buffers, references, regions)
            path = folder/rows[0]['maskFile']
            Image.fromarray(np.zeros((20, 20), dtype=np.uint8)).save(path)
            report['rois'][0]['maskSha256'] = sha(path)
            with self.assertRaises(ValueError):
                verify_masks(folder, report, buffers, references, regions)


if __name__ == '__main__':
    unittest.main()
