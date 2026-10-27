"""Compare live RenderGraph hit-distance reconstruction with isolated SDK replay."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('NrdNormalRoughness', 'NrdViewZ', 'NrdValidity', 'NrdTiles',
    'NrdSpecularCandidate', 'NrdDiffuseCandidate', 'DenoiserSpecularDistance', 'DenoiserDiffuseDistance',
    'NrdReconstructedSpecular', 'NrdReconstructedDiffuse')


def assess_reconstruction(actual, expected, candidate, states, validity, viewz):
    shape = candidate.shape
    if (len(shape) != 3 or shape[-1] != 4 or candidate.size == 0 or
            any(value.shape != shape or not np.isfinite(value).all()
                for value in (actual, expected, candidate, states, validity, viewz))):
        raise ValueError('Malformed or non-finite reconstruction data')
    if (not np.isin(validity, [0, 1]).all() or (validity[..., 3] != 0).any() or
            not np.array_equal(validity[..., 0], validity[..., 1]+validity[..., 2])):
        raise ValueError('Invalid reconstruction preparation mask')
    written = (validity[..., 0] == 1) & (viewz[..., 0] > 0) & (viewz[..., 0] < 1000)
    if (not np.array_equal(actual[written, :3], candidate[written, :3]) or
            (actual[~written] != 0).any() or (actual[..., 3] < 0).any() or (actual[..., 3] > 65504.5).any()):
        raise ValueError('RGB preservation, distance range or excluded-region clear failed')
    error = abs(actual-expected)
    if not np.array_equal(actual, expected):
        raise ValueError('Live reconstruction differs from same-device isolated SDK replay')
    missing = written & (states[..., 1] == 3)
    resolved = missing & (actual[..., 3] > 0)
    inactive = written & (states[..., 1] == 4)
    return dict(writtenPixels=int(written.sum()), missingDistances=int(missing.sum()),
        reconstructed=int(resolved.sum()), unresolved=int((missing & ~resolved).sum()),
        inactivePixels=int(inactive.sum()), maximumAbsoluteError=float(error.max()), rgbPreserved=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--disabled-distance-diagnostics', action='store_true')
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    write_json(output/'scene.json', json.loads((source/'scene.json').read_text()))
    preset_path = source/('preset.json' if (source/'preset.json').exists() else 'on.json')
    preset = json.loads(preset_path.read_text())
    preset['pathTracing']['reconstructionDiagnostics'] = True
    if args.disabled_distance_diagnostics:
        preset['pathTracing']['denoiserDistanceDiagnostics'] = False
    write_json(output/'preset.json', preset)
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    harness = ROOT/'build/Debug/RtPbrSurvey.NrdReconstructionTests.exe'
    shader = ROOT/'build/Tests/NrdPacking/RELAX_Reconstruction_mode0.cso'
    report = dict(schemaVersion=1, status='running', inputReady=False, records=[], failures=[],
        exeSha256=sha(exe), harnessSha256=sha(harness), isolatedShaderSha256=sha(shader),
        runtimeShaderSha256=sha(ROOT/'build/shaders/shaders_NrdReconstruction_main.cso'),
        conversionShaderSha256=sha(ROOT/'build/shaders/shaders_NrdConversion_CSMain.cso'),
        scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'))
    try:
        buffers, metadata = {}, None
        names = NAMES if not args.disabled_distance_diagnostics else (
            'NrdValidity', 'NrdReconstructedSpecular', 'NrdReconstructedDiffuse')
        for name in names:
            buffers[name], record = capture(exe, output, name, output/'preset.json', name)
            report['records'].append(record)
            other = buffers[name][0]
            if other['resource'] != 'PathTracing.'+name or other['format'] != (41 if name == 'NrdTiles' else 2):
                raise ValueError('Unexpected resource or native format: '+name)
            if metadata is None:
                metadata = other
            ignore = ('resource', 'format', 'width', 'height') if name == 'NrdTiles' else ('resource', 'format')
            if {k: v for k, v in other.items() if k not in ignore} != {
                    k: v for k, v in metadata.items() if k not in ignore}:
                raise ValueError('Capture cohort mismatch: '+name)
            write_json(output/'report.json', report)
        if args.disabled_distance_diagnostics:
            if any((buffers[name][1] != 0).any() for name in names):
                raise ValueError('Disabled diagnostics leaked reconstruction output')
            report.update(status='passed', excludedPixels=metadata['width']*metadata['height'],
                scope='disabled-input-exclusion')
        else:
            width, height = metadata['width'], metadata['height']
            tiles = buffers['NrdTiles'][1]
            if tiles.shape != ((height+15)//16, (width+15)//16, 1) or (tiles != 0).any():
                raise ValueError('Invalid all-active tile mask')
            raw = dict(tiles=tiles, normals=buffers['NrdNormalRoughness'][1],
                viewz=buffers['NrdViewZ'][1][..., :1], specular=buffers['NrdSpecularCandidate'][1],
                diffuse=buffers['NrdDiffuseCandidate'][1])
            for name, data in raw.items():
                data.astype('<f4').tofile(output/(name+'.raw'))
            command = [str(harness), str(shader), '3x3', '--replay', str(output), str(width), str(height), '--hardware']
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
            (output/'replay.log').write_text(run.stdout+run.stderr, encoding='utf-8')
            if run.returncode != 0 or 'no D3D12 errors' not in run.stdout:
                raise ValueError('Isolated SDK replay failed: '+run.stdout+run.stderr)
            trace_lines = [line[len('[PathTracing] '):] for line in
                (output/(names[0]+'.log')).read_text(encoding='utf-8-sig').splitlines()
                if line.startswith('[PathTracing] ')]
            adapter = json.loads(trace_lines[-1])['adapter']['name']
            if 'Adapter: '+adapter not in run.stdout:
                raise ValueError('Live and replay GPU adapters differ')
            report['replay'] = dict(command=command, d3d12Errors=0, adapter=adapter,
                comparator='bit-exact', logSha256=sha(output/'replay.log'),
                inputSha256={name: sha(output/(name+'.raw')) for name in raw})
            report['lobes'] = {}
            for lobe in ('Specular', 'Diffuse'):
                path = output/(lobe.lower()+'-3x3.raw')
                expected = np.fromfile(path, dtype='<f4').reshape(height, width, 4)
                report['lobes'][lobe] = assess_reconstruction(buffers['NrdReconstructed'+lobe][1], expected,
                    buffers['Nrd'+lobe+'Candidate'][1], buffers['Denoiser'+lobe+'Distance'][1],
                    buffers['NrdValidity'][1], buffers['NrdViewZ'][1])
                report['lobes'][lobe]['replayOutputSha256'] = sha(path)
            if not any(lobe['reconstructed'] > 0 for lobe in report['lobes'].values()):
                raise ValueError('Fixture did not exercise missing-distance reconstruction')
            report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed')
        report['failures'].append(str(error))
    report['limitation'] = ('Live 3x3 spatial reconstruction only, denoising range 1000. '
        'Raw SDK outputs can contain distances for inactive lobes; unresolved active distances remain zero. '
        'No full RELAX dispatch, temporal history, composition or input-readiness certification.')
    write_json(output/'report.json', report)
    print(report['status'], report.get('lobes'), report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
