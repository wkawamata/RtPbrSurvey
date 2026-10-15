"""Capture the application's PT conversion candidates and compare isolated SDK arithmetic."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json
from replay_nrd_conversion import make_inputs

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('NormalRoughness', 'Albedo', 'MaterialF0Metallic', 'PhysicalDiffuse', 'PhysicalSpecular',
    'DenoiserDiffuseDistance', 'DenoiserSpecularDistance', 'DenoiserPrimaryPosition',
    'NrdDiffuseCandidate', 'NrdSpecularCandidate', 'NrdConversionDiagnostics')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--disabled-distance-diagnostics', action='store_true')
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    write_json(output/'scene.json', json.loads((source/'scene.json').read_text()))
    preset = json.loads((source/'on.json').read_text())
    if args.disabled_distance_diagnostics:
        preset['pathTracing']['denoiserDistanceDiagnostics'] = False
    write_json(output/'preset.json', preset)
    report = dict(schemaVersion=1, status='running', inputReady=False, runtimeGeometrySnapshotVerified=False,
        exeSha256=sha(exe), shaderSha256=sha(ROOT/'build/shaders/shaders_NrdConversion_CSMain.cso'),
        sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'), records=[], failures=[])
    try:
        buffers = {}
        meta = None
        names = NAMES if not args.disabled_distance_diagnostics else NAMES[-3:]
        for name in names:
            buffers[name], record = capture(exe, output, name, output/'preset.json', name)
            report['records'].append(record)
            other = buffers[name][0]
            if meta is None:
                meta = other
            if {k: v for k, v in meta.items() if k not in ('resource', 'format')} != {
                    k: v for k, v in other.items() if k not in ('resource', 'format')}:
                raise ValueError('Runtime capture cohort mismatch: '+name)
            write_json(output/'report.json', report)
        diagnostics = buffers['NrdConversionDiagnostics'][1]
        codes = diagnostics[..., 0]
        if args.disabled_distance_diagnostics:
            if (not (codes == 10).all() or (diagnostics[..., 1:] != 0).any() or
                    (buffers['NrdDiffuseCandidate'][1] != 0).any() or
                    (buffers['NrdSpecularCandidate'][1] != 0).any()):
                raise ValueError('Disabled distance diagnostics leaked conversion candidates')
            report.update(status='passed', hostGate=10, excludedPixels=int(codes.size))
            write_json(output/'report.json', report)
            print('passed disabled distance gate, pixels='+str(codes.size))
            return 0
        if not np.isin(codes, [0, 1, 4, 5, 6, 7]).all() or (diagnostics[..., 3] != 0).any():
            raise ValueError('Unexpected host gate or runtime readiness claim')
        accepted = np.isin(codes, [0, 7])
        if not accepted.any():
            raise ValueError('No convertible primary pixels were exercised')
        inputs = make_inputs(meta, buffers, False)
        inputs.tofile(output/'isolated-input.raw')
        harness = ROOT/'build/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
        shader = ROOT/'build/Tests/NrdPacking/shaders_NrdCapturedConversion_CSMain.cso'
        run = subprocess.run([str(harness), str(shader), '--input-conversion', '--input', str(output/'isolated-input.raw'),
            '--output', str(output/'isolated-output.raw')], capture_output=True, text=True, timeout=120, cwd=ROOT)
        if run.returncode:
            raise ValueError('Isolated comparison failed: '+run.stdout+run.stderr)
        expected = np.fromfile(output/'isolated-output.raw', dtype='<f4').reshape(meta['height'], meta['width'], 4, 4)
        errors = {}
        for index, lobe in enumerate(('Diffuse', 'Specular')):
            actual = buffers['Nrd'+lobe+'Candidate'][1]
            if (actual[~accepted] != 0).any():
                raise ValueError('Runtime rejected pixel leaked '+lobe)
            error = abs(actual-expected[..., index, :])
            if (error[accepted] > 2e-5+2e-5*abs(expected[accepted, index, :])).any():
                raise ValueError('Runtime conversion disagrees with isolated '+lobe)
            errors[lobe] = float(error[accepted].max())
        report.update(status='passed', diagnosticCounts={str(code): int((codes == code).sum()) for code in [0, 1, 4, 5, 6, 7]},
            acceptedPixels=int(accepted.sum()), maximumPackedError=errors)
    except Exception as error:
        report['failures'].append(str(error))
        report['status'] = 'failed'
    report['limitation'] = ('Experimental runtime conversion candidates only; Native remains the final image. '
        'Environment range policy, runtime readiness, reconstruction and full RELAX dispatch are not certified.')
    write_json(output/'report.json', report)
    print(report['status'], report.get('diagnosticCounts'), report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
