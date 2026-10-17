"""Run the production admission helper on WARP and hardware with explicit failure vectors."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_nrd_runtime_admission import expected_admission
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    base = np.array([[1, 2, 3, 4], [1, 2, 3, 4], [4, 1, 4, 0], [4, 1, 4, 0],
        [1, 1, 0, 0], [2, 0, 0, 0], [0, 0, 0, 0]], dtype=np.float32)
    cases = [('active', [], 0),
        ('inactive-diffuse', [(0, slice(None), [0, 0, 0, 5]), (2, slice(None), [0, 4, 0, 0])], 0),
        ('unresolved-diffuse', [(0, 3, 0), (2, slice(None), [0, 3, 0, 0])], 4),
        ('unresolved-specular', [(1, 3, 0), (3, slice(None), [0, 3, 0, 0])], 4),
        ('excluded', [(4, slice(None), 0)], 1), ('invalid-mask', [(4, 1, 0)], 2),
        ('nonfinite-depth', [(5, 0, np.nan)], 2), ('nonfinite-rgb', [(0, 0, np.nan)], 3),
        ('negative-rgb', [(0, 1, -1)], 3), ('overflow-distance', [(0, 3, 1e20)], 3),
        ('inactive-rgb', [(2, slice(None), [0, 4, 0, 0])], 3),
        ('untraced', [(2, slice(None), 0)], 3), ('invalid-hit-state', [(2, 0, 0)], 3),
        ('packing-ceiling', [(0, 3, 65504.02)], 0), ('outside-range', [(5, 0, 1000)], 1),
        ('invalid-over-unresolved', [(0, 3, 0), (2, slice(None), [0, 3, 0, 0]), (1, 0, -1)], 3)]
    rows = []
    for name, edits, code in cases:
        row = base.copy()
        for field, channel, value in edits:
            row[field, channel] = value
        rows.append(row)
    inputs = np.stack(rows)
    path = output/'inputs.raw'
    inputs.astype('<f4').tofile(path)
    expected = expected_admission(*(inputs[None, :, index, :] for index in range(6)))
    if list(expected[3][0, :, 1]) != [case[2] for case in cases]:
        raise ValueError('CPU reference disagrees with explicit vector outcomes')
    expected = np.stack(expected, axis=2).reshape(len(cases), 16)
    exe = ROOT/'build/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
    shader = ROOT/'build/Tests/shaders_NrdAdmissionVectors_CSMain.cso'
    report = dict(schemaVersion=1, status='running', cases=[dict(name=name, expectedCode=code)
        for name, _, code in cases], runs=[], failures=[], inputReady=False,
        harnessSha256=sha(exe), shaderSha256=sha(shader), inputSha256=sha(path),
        helperSha256=sha(ROOT/'Shaders/NrdRelaxInputAdmission.hlsli'), scriptSha256=sha(Path(__file__)))
    try:
        for mode in ('warp', 'hardware'):
            result = output/(mode+'.raw')
            command = [str(exe), str(shader), '--input-conversion', '--input', str(path), '--output', str(result)]
            if mode == 'hardware':
                command.append('--hardware')
            run = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (output/(mode+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
            if run.returncode != 0:
                raise ValueError('GPU vector harness failed: '+mode+' '+run.stdout+run.stderr)
            actual = np.fromfile(result, dtype='<f4').reshape(len(cases), 16)
            if not np.array_equal(actual, expected):
                raise ValueError('GPU admission vector mismatch: '+mode)
            report['runs'].append(dict(mode=mode, command=command, comparator='bit-exact', sha256=sha(result)))
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['failures'].append(str(error))
    write_json(output/'report.json', report)
    print(report['status'], len(cases), 'vectors', report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
