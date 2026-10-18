"""Run actual RELAX GPU constant-field controls and independently check all readback values."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_part1 import sha, write_json

EXPECTED = {(case, frame) for case in range(4) for frame in (1, 30, 120)}


def assess(report):
    rows = report.get('checkpoints', [])
    keys = [(row.get('case'), row.get('elapsedFrames')) for row in rows]
    if (len(keys) != 12 or set(keys) != EXPECTED or report.get('schemaVersion') != 1 or
            report.get('width') != 64 or report.get('height') != 64 or report.get('sdkVersion') != '4.17.3' or
            report.get('prepass') != [30, 50] or report.get('normalEncoding') != 4 or
            report.get('roughnessEncoding') != 1 or report.get('d3d12Errors') != 0 or report.get('failures') != []):
        raise ValueError('Incomplete, duplicate or incompatible constant-control report')
    results = []
    for row in rows:
        case, frames = row['case'], row['elapsedFrames']
        if (row.get('sdkFrameIndex') != frames-1 or row.get('reset') is not (frames == 1) or
                type(row.get('dispatchCount')) is not int or row['dispatchCount'] <= 0):
            raise ValueError('Invalid constant-control runtime checkpoint')
        for lobe in ('diffuse', 'specular'):
            expected = [0., 0, 0]
            if lobe == 'diffuse' and case in (1, 3):
                expected = [.125, .5, 2]
            if lobe == 'specular' and case in (2, 3):
                expected = [2, .25, .125]
            data = row[lobe]
            if data.get('expectedRgb') != expected:
                raise ValueError('Constant-control expected input changed')
            samples = np.asarray(data.get('rgbSamples'), dtype=float)
            if samples.shape != (64*64*3,) or not np.isfinite(samples).all() or (samples < 0).any():
                raise ValueError('Missing or invalid actual GPU readback')
            samples = samples.reshape(-1, 3)
            mean = samples.mean(axis=0)
            maximum = np.abs(samples-np.asarray(expected)).max(axis=0)
            bound = np.where(np.asarray(expected) == 0, 1e-6, .004*np.maximum(1, expected))
            passed = bool((maximum <= bound).all())
            if (data.get('nonfiniteValues') != 0 or data.get('passed') is not passed or
                    not np.allclose(mean, data.get('meanRgb'), rtol=0, atol=1e-12) or
                    not np.allclose(maximum, data.get('maximumAbsoluteErrorRgb'), rtol=0, atol=1e-12)):
                raise ValueError('GPU report metrics disagree with actual readback')
            results.append(dict(case=case, frames=frames, lobe=lobe, meanRgb=mean.tolist(),
                maximumAbsoluteErrorRgb=maximum.tolist(), boundRgb=bound.tolist(), passed=passed))
    passed = all(row['passed'] for row in results)
    if report.get('status') != ('passed-constant-signal-controls' if passed else 'failed-constant-signal-controls'):
        raise ValueError('Constant-control status disagrees with evidence')
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    exe, shaders = args.executable.resolve(strict=True), args.shaders.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Output must be new/empty')
    output.mkdir(parents=True, exist_ok=True)
    paths = [exe, Path(__file__), shaders/'nrd-relax-manifest.txt'] + sorted(shaders.glob('nrd-relax-*.cso'))
    if len(paths) != 30:
        parser.error('Expected manifest and 27 pinned SDK shaders')
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, assessments=[], failures=[],
        fullIntegrationReady=False, limitation='Constant inputs only; not a general NRD quality or PT integration verdict.')
    write_json(output/'report.json', report)
    try:
        cmd = [str(exe), str(shaders), str(output/'gpu-report.json')]
        report['command'] = cmd
        with (output/'stdout.log').open('w', encoding='utf-8') as stdout, (
                output/'stderr.log').open('w', encoding='utf-8') as stderr:
            child = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=180)
        report['exitCode'] = child.returncode
        report['assessments'] = assess(json.loads((output/'gpu-report.json').read_text(encoding='utf-8')))
        passed = all(row['passed'] for row in report['assessments'])
        if child.returncode != (0 if passed else 1):
            raise ValueError('GPU runner exit code disagrees with result')
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Control artifacts changed')
        report['evidenceSha256'] = {name: sha(output/name) for name in ('gpu-report.json', 'stdout.log', 'stderr.log')}
        report['status'] = 'passed-constant-controls' if passed else 'failed-constant-controls'
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-constant-controls'


if __name__ == '__main__':
    raise SystemExit(main())
