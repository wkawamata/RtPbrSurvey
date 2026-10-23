import copy
import json
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

import numpy as np

from requalify_nrd_glossy import RESOURCES, LIMITS, DEPTHS, SEEDS, capture_reference, read_evidence, validate_inventory
from validate_part1 import sha


class GlossyRequalificationTests(unittest.TestCase):
    def test_fresh_reference_records_fixed_samples_depth_and_vsync_override(self):
        scene = dict(renderPreset='source.json')
        preset = dict(pathTracing=dict(maxBounces=4, nrdPreview=True, denoiserDistanceDiagnostics=True, randomSeed=7))
        with tempfile.TemporaryDirectory() as directory, patch('requalify_nrd_glossy.subprocess.Popen') as start, \
                patch('requalify_nrd_glossy.sha', return_value='digest'), \
                patch('requalify_nrd_glossy.read_evidence', return_value=({'meta': 1}, np.zeros((1, 1, 4)), {'runtime': 1})), \
                patch('requalify_nrd_glossy.check_reference') as check:
            child = start.return_value
            child.wait.return_value = 0
            child.poll.return_value = 0
            _, record = capture_reference(Path(directory), scene, preset, {'target': 1}, 8, 29)
            command = start.call_args.args[0]
            self.assertIn('-DisableVSync', command)
            self.assertEqual(command[command.index('-PathTracingSamples')+1], '8192')
            saved = json.loads((Path(directory)/'reference-b8-29-preset.json').read_text(encoding='utf-8'))
            self.assertEqual(saved['pathTracing']['maxBounces'], 8)
            self.assertFalse(saved['pathTracing']['nrdPreview'])
            self.assertEqual(record['command'], command)
            self.assertEqual(check.call_args.kwargs, dict(environment_mode=7, sample_count=8192, max_bounces=8))
            self.assertEqual(preset['pathTracing']['maxBounces'], 4)
            self.assertTrue(preset['pathTracing']['nrdPreview'])
            self.assertEqual(scene['renderPreset'], 'source.json')
            child.kill.assert_not_called()

    def test_failed_or_timed_out_reference_is_not_accepted(self):
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as directory, \
                    patch('requalify_nrd_glossy.subprocess.Popen') as start:
                child = start.return_value
                child.wait.side_effect = [subprocess.TimeoutExpired('app', 1000), 0] if timeout else [2]
                child.poll.return_value = None if timeout else 2
                with self.assertRaises(subprocess.TimeoutExpired if timeout else ValueError):
                    capture_reference(Path(directory), {}, dict(pathTracing={}), {}, 4, 11)
                if timeout:
                    child.kill.assert_called_once()
                else:
                    child.kill.assert_not_called()

    def inventory(self):
        labels = list(RESOURCES)+['zero-environment']+[
            'reference-b'+str(depth)+'-'+str(seed) for depth in DEPTHS for seed in SEEDS]
        return dict(schemaVersion=1, records=[dict(label=label) for label in labels], failures=[],
            maxBounces=4, referenceDepths=list(DEPTHS), referenceSeeds=list(SEEDS),
            referenceSamples=4096, limits=copy.deepcopy(LIMITS))

    def test_inventory_requires_exact_depth_seed_resource_set(self):
        validate_inventory(self.inventory())
        for mutation in ('missing', 'duplicate', 'unknown', 'depth', 'samples', 'seed', 'limits', 'failure'):
            report = self.inventory()
            if mutation == 'missing':
                report['records'].pop()
            elif mutation == 'duplicate':
                report['records'][-1] = report['records'][0]
            elif mutation == 'unknown':
                report['records'][-1]['label'] = 'unrelated'
            elif mutation == 'depth':
                report['maxBounces'] = 2
            elif mutation == 'samples':
                report['referenceSamples'] = 1024
            elif mutation == 'seed':
                report['referenceSeeds'] = [11, 11]
            elif mutation == 'limits':
                report['limits']['referencePairNrmse'] = .04
            else:
                report['failures'] = ['failed']
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_inventory(report)

    def test_evidence_requires_hashes_identity_and_single_clean_runtime_record(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            path, log = folder/'case.ptbuf', folder/'case.log'
            path.write_bytes(b'raw evidence placeholder')
            diagnostics = dict(maxBounces=4)
            line = '[PathTracing] '+json.dumps(diagnostics)+'\n'
            log.write_text(line, encoding='utf-8')
            record = dict(label='case', d3d12Errors=0, sha256=sha(path), logSha256=sha(log), diagnostics=diagnostics)
            image = np.zeros((2, 2, 4))
            with patch('requalify_nrd_glossy.read_buffer', return_value=({'resource': 'test'}, image)):
                meta, actual, runtime = read_evidence(folder, 'case', record)
                self.assertEqual(runtime, diagnostics)
                self.assertEqual(actual.dtype, np.float32)
                self.assertEqual(meta['resource'], 'test')
                for key, value in (('sha256', 'changed'), ('logSha256', 'changed'), ('label', 'other'),
                        ('d3d12Errors', 1), ('diagnostics', {})):
                    with self.subTest(key=key), self.assertRaises(ValueError):
                        read_evidence(folder, 'case', dict(record, **{key: value}))
                for text in ('[ERROR] issue\n'+line, line+line, 'missing runtime record\n'):
                    log.write_text(text, encoding='utf-8')
                    changed = dict(record, logSha256=sha(log))
                    with self.subTest(text=text), self.assertRaises(ValueError):
                        read_evidence(folder, 'case', changed)


if __name__ == '__main__':
    unittest.main()
