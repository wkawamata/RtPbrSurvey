"""Execute JSON signal-arithmetic cases on WARP with the CPU corpus values."""
import copy
import json
import math
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORPUS = Path(__file__).with_name('nrd_pixel_contract_vectors.json')


def gpu_inputs(data):
    cases, records = [], []
    for vector in data['vectors']:
        if not vector['gpuRepresentable']:
            continue
        case_id = vector['id']
        if vector['hostOnly'] or vector['expectedDisposition'] not in (
                'Convertible', 'ExcludePrimaryMiss', 'InvalidData', 'PackingRangeExceeded'):
            raise ValueError('Host-only semantics routed to GPU: '+case_id)
        if len(vector['gpuStatuses']) != 2 or len(vector['expectedPacked']) != 2:
            raise ValueError('Missing GPU expectation: '+case_id)
        record = [0.0]*28
        for index, lobe in enumerate(vector['lobes']):
            distance = lobe['distance']
            if vector['primaryHit'] and distance[1] not in (1, 4):
                raise ValueError('Unproven distance routed to GPU: '+case_id)
            if not vector['primaryHit'] and any(distance):
                raise ValueError('Contradictory primary miss: '+case_id)
            record[index*4:index*4+3] = lobe['materialFactor']
            record[(index+2)*4:(index+2)*4+3] = lobe['physical']
            record[16+index] = distance[0]
            record[20+index] = float(lobe.get('active', True))
        record[18] = float(vector['primaryHit'])
        records.append(struct.pack('<28f', *record))
        cases.append(vector)
    if not cases or len({case['id'] for case in cases}) != len(cases):
        raise ValueError('Empty or duplicate GPU case coverage')
    return cases, b''.join(records)


def assess_gpu(cases, raw):
    if len(raw) != len(cases)*64:
        raise ValueError('GPU output count mismatch')
    for index, vector in enumerate(cases):
        values = struct.unpack_from('<16f', raw, index*64)
        case_id = vector['id']
        if not all(math.isfinite(value) for value in values):
            raise ValueError('Nonfinite GPU output: '+case_id)
        for lobe in range(2):
            expected = vector['expectedPacked'][lobe]
            if len(expected) != 4 or not all(math.isfinite(value) for value in expected):
                raise ValueError('Invalid GPU expectation: '+case_id)
            if values[11+lobe*4] != vector['gpuStatuses'][lobe] or any(
                    not math.isclose(values[lobe*4+channel], expected[channel], rel_tol=1e-5, abs_tol=1e-4)
                    for channel in range(4)):
                raise ValueError('GPU contract mismatch: '+case_id)
        if any(status not in (0, 3, 4) for status in vector['gpuStatuses']) and any(values[:8]):
            raise ValueError('Partial GPU output leaked: '+case_id)
    return [case['id'] for case in cases]


def run_gpu(data, executable, shader):
    cases, raw = gpu_inputs(data)
    with tempfile.TemporaryDirectory(prefix='nrd-gpu-corpus-') as temporary:
        folder = Path(temporary)
        source, output = folder/'input.raw', folder/'output.raw'
        source.write_bytes(raw)
        result = subprocess.run([str(executable), str(shader), '--input-conversion', '--input', str(source),
            '--output', str(output)], capture_output=True, text=True, timeout=120, cwd=ROOT)
        if result.returncode:
            raise ValueError('GPU corpus dispatch failed: '+result.stdout+result.stderr)
        return assess_gpu(cases, output.read_bytes())


class NrdGpuContractSubsetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(CORPUS.read_text(encoding='utf-8'))
        build = Path(os.environ.get('RTPBRSURVEY_BUILD_DIR', str(ROOT/'build')))
        cls.executable = build/'Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
        cls.shader = build/'Tests/NrdPacking/shaders_NrdPixelContract_CSMain.cso'

    def require_gpu(self):
        if not self.executable.is_file() or not self.shader.is_file():
            self.skipTest('Build the opt-in NRD pixel contract shader and GPU harness first')

    def test_subset_routes_only_signal_cases(self):
        cases, raw = gpu_inputs(self.data)
        self.assertEqual({case['id'] for case in cases},
            {'cpu-pixel-convertible-base', 'cpu-pixel-exclude-primary-miss'})
        self.assertEqual(len(raw), len(cases)*112)

    def test_host_provenance_cannot_be_routed(self):
        data = copy.deepcopy(self.data)
        next(case for case in data['vectors'] if case['id'] == 'cpu-pixel-stale-snapshot')['gpuRepresentable'] = True
        with self.assertRaisesRegex(ValueError, 'cpu-pixel-stale-snapshot'):
            gpu_inputs(data)

    def test_json_values_execute_on_gpu(self):
        self.require_gpu()
        self.assertEqual(run_gpu(self.data, self.executable, self.shader),
            ['cpu-pixel-convertible-base', 'cpu-pixel-exclude-primary-miss'])

    def test_changed_input_or_expectation_is_detected_on_gpu(self):
        self.require_gpu()
        for change_input in (False, True):
            data = copy.deepcopy(self.data)
            case = data['vectors'][0]
            if change_input:
                case['lobes'][0]['physical'][0] += 1
            else:
                case['expectedPacked'][0][0] += 1
            with self.assertRaisesRegex(ValueError, 'cpu-pixel-convertible-base'):
                run_gpu(data, self.executable, self.shader)

    def test_one_lobe_failure_suppresses_both_gpu_outputs(self):
        self.require_gpu()
        data = copy.deepcopy(self.data)
        case = data['vectors'][0]
        case['lobes'][0]['physical'][0] = -1
        case['expectedDisposition'] = 'InvalidData'
        case['expectedPacked'] = [[0]*4, [0]*4]
        case['gpuStatuses'] = [1, 0]
        run_gpu(data, self.executable, self.shader)

    def test_overflow_suppresses_both_gpu_outputs(self):
        self.require_gpu()
        data = copy.deepcopy(self.data)
        case = data['vectors'][0]
        case['lobes'][0]['physical'][0] = 60000
        case['lobes'][0]['materialFactor'][0] = .02
        case['expectedDisposition'] = 'PackingRangeExceeded'
        case['expectedPacked'] = [[0]*4, [0]*4]
        case['gpuStatuses'] = [2, 0]
        run_gpu(data, self.executable, self.shader)


if __name__ == '__main__':
    unittest.main()
