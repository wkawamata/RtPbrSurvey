"""Exercise the production composition helper with explicit WARP/hardware vectors."""
import argparse
from pathlib import Path
import subprocess

import numpy as np

from validate_nrd_composition import expected_composition, ROOT
from validate_part1 import sha, write_json


def vectors():
    # Seven float4 slots; unused radiance alpha channels carry the four admission values.
    base = np.array([[2, 4, 8, 1], [4, 8, 16, 0], [.5, .25, .125, 1], [.25, .125, .0625, 1],
                     [1, 2, 4, 0], [2, 4, 8, 0], [9, 8, 7, 1]], dtype=np.float32)
    cases = [('active-stable-background', [], 0), ('inactive-diffuse', [(4, 3, 1)], 0),
        ('inactive-specular', [(5, 3, 1)], 0), ('excluded', [(0, 3, 0), (1, 3, 1)], 1),
        ('invalid-mask', [(0, 3, .5)], 2), ('nan-mask', [(0, 3, np.nan)], 2),
        ('bad-factor-tag', [(2, 3, 0)], 3), ('zero-factor', [(2, 0, 0)], 3),
        ('large-factor', [(3, 0, 1.1)], 3), ('nan-output', [(0, 0, np.nan)], 4),
        ('negative-output', [(1, 2, -1)], 4), ('negative-stable', [(4, 1, -1)], 4),
        ('overflow', [(0, slice(3), 3e38), (1, slice(3), 3e38), (2, slice(3), 1), (3, slice(3), 1)], 5),
        ('invalid-native', [(6, 0, np.nan)], 6), ('native-hdr', [(0, 3, 0), (6, 0, 1e20)], 1),
        ('excluded-invalid-factors', [(0, 3, 0), (2, 0, np.nan)], 1),
        ('contradictory-inactive-mask', [(0, 3, 0), (4, 3, 1)], 2)]
    rows = []
    for _, edits, _ in cases:
        row = base.copy()
        for field, channel, value in edits:
            row[field, channel] = value
        rows.append(row)
    inputs = np.stack(rows)
    mask = np.stack([inputs[:, field, 3] for field in (0, 1, 4, 5)], axis=-1)
    color, diagnostic = expected_composition(*(inputs[:, field] for field in range(7)), mask)
    if diagnostic[:, 1].tolist() != [code for _, _, code in cases]:
        raise ValueError('Independent reference disagrees with explicit classifications')
    expected = np.concatenate((color, diagnostic, np.zeros((len(cases), 8), dtype=np.float32)), axis=-1)
    return cases, inputs, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.output.resolve()
    if folder.exists() and any(folder.iterdir()):
        parser.error('Output must be empty')
    folder.mkdir(parents=True, exist_ok=True)
    cases, inputs, expected = vectors()
    inputs.astype('<f4').tofile(folder/'inputs.raw')
    exe = ROOT/'build/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
    shader = ROOT/'build/Tests/shaders_NrdCompositionVectors_CSMain.cso'
    report = dict(status='running', cases=[dict(name=name, expectedCode=code) for name, _, code in cases],
        runs=[], failures=[], harnessSha256=sha(exe), shaderSha256=sha(shader), inputSha256=sha(folder/'inputs.raw'),
        scriptSha256=sha(Path(__file__)), helperSha256=sha(ROOT/'Shaders/NrdRelaxComposition.hlsli'))
    try:
        for mode in ('warp', 'hardware'):
            output = folder/(mode+'.raw')
            command = [str(exe), str(shader), '--input-conversion', '--input', str(folder/'inputs.raw'), '--output', str(output)]
            if mode == 'hardware':
                command.append('--hardware')
            run = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (folder/(mode+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
            if run.returncode != 0:
                raise ValueError('GPU vector harness failed: '+mode+' '+run.stdout+run.stderr)
            actual = np.fromfile(output, dtype='<f4').reshape(expected.shape)
            if not np.array_equal(actual, expected):
                raise ValueError('GPU composition differs from explicit CPU outcomes: '+mode)
            report['runs'].append(dict(mode=mode, command=command, sha256=sha(output), comparator='bit-exact'))
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    write_json(folder/'report.json', report)
    print(report['status'], len(cases), 'vectors', report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
