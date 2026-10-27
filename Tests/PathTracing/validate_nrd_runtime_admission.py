"""Validate sanitized live reconstruction inputs before RELAX dispatch admission."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ('NrdReconstructedDiffuse', 'NrdReconstructedSpecular', 'DenoiserDiffuseDistance',
    'DenoiserSpecularDistance', 'NrdValidity', 'NrdViewZ')
OUTPUTS = ('NrdInputDiffuse', 'NrdInputSpecular', 'NrdInputViewZ', 'NrdInputAdmission')


def expected_admission(diffuse, specular, diffuse_state, specular_state, validity, viewz):
    values = (diffuse, specular, diffuse_state, specular_state, validity, viewz)
    shape = diffuse.shape
    if len(shape) != 3 or shape[-1] != 4 or diffuse.size == 0 or any(x.shape != shape for x in values):
        raise ValueError('Malformed input cohort')
    code = np.ones(shape[:2], dtype=np.float32)
    guide_valid = (np.isfinite(validity).all(axis=-1) & np.isin(validity, [0, 1]).all(axis=-1) &
        (validity[..., 3] == 0) & (validity[..., 0] == validity[..., 1]+validity[..., 2]) &
        np.isfinite(viewz).all(axis=-1) & (viewz[..., 1:] == 0).all(axis=-1))
    code[~guide_valid] = 2
    admitted = guide_valid & (validity[..., 0] == 1) & (viewz[..., 0] > 0) & (viewz[..., 0] < 1000)
    sanitized, inactive_masks = [], []
    bad, unresolved = np.zeros(shape[:2], dtype=bool), np.zeros(shape[:2], dtype=bool)
    for signal, state in ((diffuse, diffuse_state), (specular, specular_state)):
        inactive = state[..., 1] == 4
        invalid = (~np.isfinite(signal).all(axis=-1) | (signal < 0).any(axis=-1) |
            (signal[..., :3] > 65504).any(axis=-1) | (signal[..., 3] > 65504.5) |
            ~np.isfinite(state).all(axis=-1) | (state[..., 3] != 0) |
            ~np.isin(state[..., 1], [1, 2, 3, 4]) |
            ((state[..., 1] == 1) & ((state[..., 0] <= 0) | (state[..., 0] > 65504) | (state[..., 2] <= 0))) |
            ((state[..., 1] == 2) & ((state[..., 0] != 0) | (state[..., 2] <= 0))) |
            ((state[..., 1] >= 3) & ((state[..., 0] != 0) | (state[..., 2] != 0))) |
            (inactive & ((signal[..., :3] != 0).any(axis=-1) | (state[..., 0] != 0) | (state[..., 2] != 0))))
        bad |= invalid
        unresolved |= ~inactive & (signal[..., 3] == 0)
        packed = signal.astype(np.float32).copy()
        packed[..., 3] = np.minimum(packed[..., 3], 65504)
        packed[inactive] = 0
        sanitized.append(packed)
        inactive_masks.append(inactive)
    code[admitted & unresolved] = 4
    code[admitted & bad] = 3
    admitted &= ~bad & ~unresolved
    code[admitted] = 0
    for signal in sanitized:
        signal[~admitted] = 0
    depth = np.zeros(shape, dtype=np.float32)
    depth[..., 0] = 1e6
    depth[admitted] = viewz[admitted]
    mask = np.zeros(shape, dtype=np.float32)
    mask[..., 0] = admitted
    mask[..., 1] = code
    mask[..., 2] = admitted & inactive_masks[0]
    mask[..., 3] = admitted & inactive_masks[1]
    return (*sanitized, depth, mask)


def assess_admission(inputs, outputs):
    expected = expected_admission(*inputs)
    if len(outputs) != len(expected) or any(a.shape != e.shape or not np.array_equal(a, e)
        for a, e in zip(outputs, expected)):
        raise ValueError('Live input admission differs from the CPU contract')
    mask = expected[3]
    codes, counts = np.unique(mask[..., 1], return_counts=True)
    return dict(admittedPixels=int((mask[..., 0] == 1).sum()),
        inactiveDiffusePixels=int((mask[..., 2] == 1).sum()),
        zeroedInactiveDistances=int(((mask[..., 2] == 1) & (inputs[0][..., 3] != 0)).sum()),
        codeCounts={str(int(code)): int(count) for code, count in zip(codes, counts)}, comparator='bit-exact')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--disabled-distance-diagnostics', action='store_true')
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    scene = json.loads((source/'scene.json').read_text())
    scene['materials'][0]['metallic'] = 1.0
    preset = json.loads((source/('preset.json' if (source/'preset.json').exists() else 'on.json')).read_text())
    preset['pathTracing']['reconstructionDiagnostics'] = True
    preset['pathTracing']['denoiserDistanceDiagnostics'] = not args.disabled_distance_diagnostics
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    report = dict(schemaVersion=1, status='running', inputReady=False, records=[], failures=[],
        sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'), exeSha256=sha(exe),
        scriptSha256=sha(Path(__file__)), shaderSha256=sha(ROOT/'build/shaders/shaders_NrdAdmission_CSMain.cso'))
    write_json(output/'report.json', report)
    try:
        buffers, metadata = {}, None
        for name in INPUTS+OUTPUTS:
            (meta, raw), record = capture(exe, output, name, output/'preset.json', name)
            report['records'].append(record)
            if meta['resource'] != 'PathTracing.'+name or meta['format'] != 2:
                raise ValueError('Unexpected resource identity: '+name)
            comparable = {key: value for key, value in meta.items() if key != 'resource'}
            if metadata is not None and comparable != metadata:
                raise ValueError('Mismatched capture cohort: '+name)
            metadata = comparable
            buffers[name] = raw
            write_json(output/'report.json', report)
        assessment = assess_admission([buffers[name] for name in INPUTS], [buffers[name] for name in OUTPUTS])
        if args.disabled_distance_diagnostics:
            if assessment['admittedPixels'] != 0:
                raise ValueError('Disabled input diagnostics admitted pixels')
        elif assessment['admittedPixels'] == 0 or assessment['inactiveDiffusePixels'] == 0:
            raise ValueError('Fixture did not exercise admitted and inactive inputs')
        report.update(status='passed', assessment=assessment)
    except Exception as error:
        report['status'] = 'failed'
        report['failures'].append(str(error))
    report['limitation'] = ('Local reconstruction admission only; no camera/MV certification, '
        'full RELAX GPU dispatch or final-image composition. Raw reconstruction buffers are preserved.')
    write_json(output/'report.json', report)
    print(report['status'], report.get('assessment'), report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
