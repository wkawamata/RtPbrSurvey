import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from isolate_nrd_glossy_bias import (RESOURCES, ROI_LIMITS, SELECTION, SAMPLES,
    arithmetic, capture, check_capture, load_references, measure)
from validate_nrd_composition import NAMES, expected_composition


class GlossyBiasTests(unittest.TestCase):
    def cohort(self):
        values = {name: np.zeros((12, 20, 4), dtype=np.float32) for name in RESOURCES}
        values['SceneColor'][:] = [2, 2, 2, 1]
        values['CurrentFrameRadiance'][:] = [2, 2, 2, 1]
        values['NrdInputAdmission'][:] = [1, 0, 0, 0]
        values['NrdValidity'][:] = [1, 1, 0, 0]
        for lobe in ('Diffuse', 'Specular'):
            values['Nrd'+lobe+'Factor'][:] = [.5, .5, .5, 1]
            values['Physical'+lobe][:] = [1, 1, 1, 0]
            for name in ('Nrd'+lobe+'Candidate', 'NrdInput'+lobe, 'NrdOutput'+lobe):
                values[name][:] = [2, 2, 2, 3]
        values['NrdSceneColor'], values['NrdCompositionDiagnostics'] = expected_composition(
            *(values[name] for name in NAMES[:8]))
        return values

    def test_arithmetic_and_admission_reject_each_corruption(self):
        self.assertTrue(arithmetic(self.cohort())['admissionRgbPreserved'])
        for name in ('NrdSceneColor', 'NrdCompositionDiagnostics', 'NrdInputDiffuse',
                'NrdInputSpecular', 'CurrentFrameRadiance', 'PhysicalDiffuse'):
            values = self.cohort()
            values[name][0, 0, 0] += .1
            with self.subTest(name=name), self.assertRaises(ValueError):
                arithmetic(values)
        values = self.cohort()
        del values['Albedo']
        with self.assertRaises(ValueError):
            arithmetic(values)

    def test_measure_records_lobes_without_promoting_bad_quality(self):
        values = self.cohort()
        mask = np.ones((12, 20), dtype=bool)
        references = [values['SceneColor'], values['SceneColor']]
        row = measure(values, values['SceneColor'], references, mask)
        self.assertEqual(row['status'], 'passed-roi')
        self.assertEqual(row['restoredLobeMeanRgb'], dict(Diffuse=[1, 1, 1], Specular=[1, 1, 1]))
        refs = [value*1.2 for value in references]
        row = measure(values, values['SceneColor'], refs, mask)
        self.assertEqual(row['status'], 'failed-roi-quality')
        values['NrdCompositionDiagnostics'][0, 0, 0] = 0
        with self.assertRaises(ValueError):
            measure(values, values['SceneColor'], references, mask)

    def metadata(self):
        target = dict(width=20, height=12, rayTMin=.001, rayTMax=100, normalBias=.001,
            lensShift=[0, 0], viewProjection=list(range(16)), inverseViewProjection=list(range(16)))
        meta = dict(target, resource='PathTracing.NrdSceneColor', format=2, sampleStartIndex=30,
            primarySampling='pixel-center', samplesPerFrame=1, randomSeed=7,
            denoiserDistanceDiagnostics=True, nrdPreviewRequested=True,
            nrdPresentation='NRD RELAX diagnostic (per-pixel Native fallback)', nrdFailure='',
            nrdLastEvaluation=dict(frameIndex=30, recorded=True))
        diagnostics = dict(accumulatedSamples=30, targetSamples=0, maxBounces=4, randomSeed=7,
            samplesPerFrame=1, environmentEnabled=True, environmentSamplingMode=7,
            directLightingEnabled=False, emissiveEnabled=False, russianRoulette=False,
            denoiserDistanceDiagnostics=True, nrdFailureCount=0)
        return meta, diagnostics, target

    @patch('isolate_nrd_glossy_bias.assess_frame')
    def test_checkpoint_rejects_camera_sampling_transport_and_history_changes(self, assess):
        meta, diagnostics, target = self.metadata()
        common = check_capture(meta, diagnostics, 'NrdSceneColor', 30, target, None)
        check_capture(meta, diagnostics, 'NrdSceneColor', 30, target, common)
        for field, value in (('resource', 'PathTracing.SceneColor'), ('format', 10), ('sampleStartIndex', 29),
                ('randomSeed', 11), ('width', 21), ('viewProjection', [0]*16), ('nrdFailure', 'failed'),
                ('nrdLastEvaluation', dict(frameIndex=1, recorded=True))):
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_capture(dict(meta, **{field: value}), diagnostics, 'NrdSceneColor', 30, target, None)
        with self.assertRaises(ValueError):
            check_capture(meta, dict(diagnostics, environmentSamplingMode=5), 'NrdSceneColor', 30, target, None)
        with self.assertRaises(ValueError):
            check_capture(meta, diagnostics, 'NrdSceneColor', 30, target, {})

    def test_capture_process_failure_and_timeout_cleanup(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as directory, \
                    patch('isolate_nrd_glossy_bias.subprocess.Popen') as start:
                child = start.return_value
                child.wait.side_effect = [subprocess.TimeoutExpired('app', 180), 0] if timeout else [2]
                child.poll.return_value = None if timeout else 2
                with self.assertRaises(subprocess.TimeoutExpired if timeout else ValueError):
                    capture(Path(directory), 'NrdSceneColor', 120, {}, None)
                command = start.call_args.args[0]
                self.assertEqual(command[command.index('-CaptureAfterFrames')+1], '120')
                self.assertEqual(command[-1], '-DisableVSync')
                self.assertEqual(child.kill.call_count, int(timeout))

    def test_refinement_inventory_rejects_missing_duplicate_or_changed_evidence(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch('isolate_nrd_glossy_bias.identities', return_value={}), \
                patch('isolate_nrd_glossy_bias.sha', return_value='digest'), \
                patch('isolate_nrd_glossy_bias.load_fixed', side_effect=RuntimeError('validated inventory')) as load:
            folder = Path(directory)
            report = dict(schemaVersion=1, sourceEvidence=str(folder), failures=[], artifactSha256={},
                sourceReportSha256='digest', referenceSamples=SAMPLES, referenceSeeds=[11, 29], maxBounces=4,
                limits=ROI_LIMITS, selection=SELECTION, sourceRecordsRevalidated=16, freshGpuCaptures=2,
                candidateGpuCaptures=0, masksReselected=False, sourceEvidenceReexecuted=False,
                fullIntegrationReady=False, subjectiveVerdict='unanswered',
                records=[dict(label='reference-b4-11'), dict(label='reference-b4-29')])
            (folder/'report.json').write_text(json.dumps(report), encoding='utf-8')
            with self.assertRaisesRegex(RuntimeError, 'validated inventory'):
                load_references(folder)
            for mutation in ('missing', 'duplicate', 'samples', 'masks', 'hash', 'failure'):
                changed = copy.deepcopy(report)
                if mutation == 'missing':
                    changed['records'].pop()
                elif mutation == 'duplicate':
                    changed['records'][-1] = changed['records'][0]
                elif mutation == 'samples':
                    changed['referenceSamples'] = 8192
                elif mutation == 'masks':
                    changed['masksReselected'] = True
                elif mutation == 'hash':
                    changed['sourceReportSha256'] = 'changed'
                else:
                    changed['failures'] = ['failed']
                load.reset_mock()
                (folder/'report.json').write_text(json.dumps(changed), encoding='utf-8')
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    load_references(folder)
                load.assert_not_called()


if __name__ == '__main__':
    unittest.main()
