"""Measure known balanced noise through RELAX; keep execution and quality verdicts separate."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from run_nrd_constant_controls import assess as assess_constants
from validate_part1 import sha, write_json

SEEDS = (7, 101, 307)
AMPLITUDES = (0., .25, .75)
FRAMES = (1, 30, 120)
BASE = (np.array([.125, .5, 2]), np.array([2, .25, .125]))


def signal(seed, amplitude, frame, lobe):
    pixels = np.arange(4096, dtype=np.uint64)
    channels = np.arange(3, dtype=np.uint64)
    key = (np.uint64(seed) ^ ((pixels//2)[:, None]*0x9e3779b9) ^
        np.uint64(frame*0x85ebca6b) ^ np.uint64(lobe*0xc2b2ae35) ^ (channels[None, :]*0x27d4eb2f)) & 0xffffffff
    key ^= key >> 16
    key = (key*0x7feb352d) & 0xffffffff
    key ^= key >> 15
    key = (key*0x846ca68b) & 0xffffffff
    key ^= key >> 16
    positive = ((key & 1) != 0) ^ ((pixels[:, None] & 1) != 0)
    return (BASE[lobe] * (1 + amplitude*np.where(positive, 1, -1))).astype(np.float32)


def pixels(data, expected):
    values = np.asarray(data.get('rgbSamples'), dtype=float)
    if values.shape != (4096*3,) or not np.isfinite(values).all() or (values < 0).any():
        raise ValueError('Missing/nonfinite/negative noise-control readback')
    values = values.reshape(4096, 3)
    mean = values.mean(axis=0)
    maximum = np.abs(values-expected).max(axis=0)
    bound = .004*np.maximum(1, expected)
    constant_pass = bool((maximum <= bound).all())
    if (data.get('expectedRgb') != expected.tolist() or data.get('nonfiniteValues') != 0 or
            data.get('passed') is not constant_pass or
            not np.allclose(mean, data.get('meanRgb'), rtol=0, atol=1e-12) or
            not np.allclose(maximum, data.get('maximumAbsoluteErrorRgb'), rtol=0, atol=1e-12)):
        raise ValueError('Noise-control summary differs from actual readback')
    return values


def metrics(values, expected):
    scale = float(np.linalg.norm(expected))
    return dict(meanRgb=values.mean(axis=0).tolist(),
        nrmse=float(np.sqrt(np.mean(np.sum((values-expected)**2, axis=1)))/scale),
        meanRgbBias=float(np.linalg.norm(values.mean(axis=0)-expected)/scale))


def assess(report, constant):
    assess_constants(constant)
    rows = report.get('checkpoints', [])
    keys = [(row.get('seed'), row.get('amplitude'), row.get('elapsedFrames')) for row in rows]
    expected_keys = {(seed, amplitude, frame) for seed in SEEDS for amplitude in AMPLITUDES for frame in FRAMES}
    if (len(keys) != 27 or set(keys) != expected_keys or report.get('mode') != 'balanced-pair-noise' or
            report.get('status') != 'completed-noise-controls' or report.get('d3d12Errors') != 0 or
            report.get('failures') != [] or report.get('seeds') != list(SEEDS) or
            report.get('amplitudes') != list(AMPLITUDES)):
        raise ValueError('Incomplete/duplicate or unexecuted noise-control inventory')
    for key in ('width', 'height', 'sdkVersion', 'prepass', 'normalEncoding', 'roughnessEncoding',
                'inputFormat', 'outputFormat', 'adapter'):
        if report.get(key) != constant.get(key):
            raise ValueError('Noise/control configuration mismatch: '+key)
    result = []
    for row in rows:
        seed, amplitude, frame = row['seed'], row['amplitude'], row['elapsedFrames']
        if (row.get('case') != 3 or row.get('sdkFrameIndex') != frame-1 or
                row.get('reset') is not (frame == 1) or type(row.get('dispatchCount')) is not int or
                row['dispatchCount'] <= 0):
            raise ValueError('Wrong noise-control runtime checkpoint')
        for lobe, name in enumerate(('diffuse', 'specular')):
            base = BASE[lobe]
            output = pixels(row[name], base)
            current = pixels(row['input'+name.capitalize()], base)
            expected_input = signal(seed, amplitude, frame-1, lobe)
            if not np.array_equal(current, expected_input) or not np.array_equal(current.mean(axis=0), base):
                raise ValueError('Actual GPU input does not match declared balanced noise')
            temporal = np.asarray(row.get('temporalNative'+name.capitalize()), dtype=float)
            generated = sum((signal(seed, amplitude, f, lobe).astype(float) for f in range(frame)))/frame
            if temporal.shape != (4096*3,) or not np.array_equal(temporal.reshape(4096, 3), generated):
                raise ValueError('Temporal Native comparator does not match full input sequence')
            temporal = temporal.reshape(4096, 3)
            if amplitude == 0:
                control_row = next(r for r in constant['checkpoints'] if r['case'] == 3 and r['elapsedFrames'] == frame)
                if not np.array_equal(output, np.asarray(control_row[name]['rgbSamples']).reshape(4096, 3)):
                    raise ValueError('Noise-free control changed through upload/replay path')
            out, raw, native = (metrics(value, base) for value in (output, current, temporal))
            evaluated = amplitude == 0 or frame in (30, 120)
            passed = (bool((np.abs(output-base) <= .004*np.maximum(1, base)).all()) if amplitude == 0 else
                out['meanRgbBias'] <= .05 and out['nrmse'] <= raw['nrmse'])
            result.append(dict(seed=seed, amplitude=amplitude, frames=frame, lobe=name,
                output=out, input=raw, temporalNative=native, evaluated=evaluated,
                passed=passed if evaluated else None, beatsTemporalNative=out['nrmse'] <= native['nrmse']))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--constant', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    exe, shaders, control = (p.resolve(strict=True) for p in (args.executable, args.shaders, args.constant))
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Output must be new/empty')
    output.mkdir(parents=True, exist_ok=True)
    paths = [exe, Path(__file__), Path(__file__).with_name('run_nrd_constant_controls.py'),
        control/'report.json', shaders/'nrd-relax-manifest.txt']+sorted(shaders.glob('nrd-relax-*.cso'))
    if len(paths) != 32:
        parser.error('Expected 27 SDK shaders')
    root = Path(__file__).resolve().parents[2]
    paths += [root/'Tests/NrdConstantSignalTests.cpp', root/'doc/branch/feature/nrd-known-noise-controls.md']
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated', artifactSha256=artifacts,
        assessments=[], failures=[], fullIntegrationReady=False,
        criteria='Zero amplitude: constant bounds; noisy frames 30/120: mean RGB bias <= 5% and RMSE <= current input.',
        limitation='Balanced adjacent pairs, not independent white/PT noise; fixed guides, static known field. '
            'Temporal Native is descriptive, not a mandatory quality gate or equal-work comparator.')
    write_json(output/'report.json', report)
    try:
        previous = json.loads((control/'report.json').read_text(encoding='utf-8'))
        if previous.get('status') != 'passed-constant-controls':
            raise ValueError('Constant controls must pass first')
        for p, digest in previous['artifactSha256'].items():
            if sha(Path(p)) != digest:
                raise ValueError('Constant control artifact changed')
        for name, digest in previous['evidenceSha256'].items():
            if sha(control/name) != digest:
                raise ValueError('Constant raw evidence changed')
        constant = json.loads((control/'gpu-report.json').read_text(encoding='utf-8'))
        cmd = [str(exe), str(shaders), str(output/'gpu-report.json'), '--noise']
        report['command'] = cmd
        with (output/'stdout.log').open('w', encoding='utf-8') as stdout, (output/'stderr.log').open('w', encoding='utf-8') as stderr:
            child = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=180)
        report['exitCode'] = child.returncode
        if child.returncode != 0:
            raise ValueError('GPU noise execution failed')
        report['assessments'] = assess(json.loads((output/'gpu-report.json').read_text(encoding='utf-8')), constant)
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Noise artifacts changed during execution')
        report['evidenceSha256'] = {name: sha(output/name) for name in ('gpu-report.json', 'stdout.log', 'stderr.log')}
        passed = all(row['passed'] for row in report['assessments'] if row['evaluated'])
        report.update(status='completed-known-noise-study', qualityVerdict='passed-known-noise-cases' if passed else 'failed-known-noise-cases')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['status'] != 'completed-known-noise-study'


if __name__ == '__main__':
    raise SystemExit(main())
