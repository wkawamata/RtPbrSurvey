"""Validate live reconstruction guide preparation, without claiming RELAX readiness."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('NormalRoughness', 'ViewZ', 'DenoiserPrimaryPosition', 'NrdConversionDiagnostics',
    'NrdNormalRoughness', 'NrdViewZ', 'NrdValidity')


def validate_guides(buffers):
    arrays = {name: buffers[name][1] for name in NAMES}
    diagnostics = arrays['NrdConversionDiagnostics']
    if diagnostics.ndim != 3 or diagnostics.size == 0:
        raise ValueError('Empty or malformed guide cohort')
    shape = diagnostics.shape[:2]
    for name, value in arrays.items():
        channels = 1 if name == 'ViewZ' else 4
        if value.shape != shape+(channels,) or not np.isfinite(value).all():
            raise ValueError('Invalid guide shape or non-finite data: '+name)
    codes = diagnostics[..., 0]
    if not np.isin(codes, [0, 4, 6, 7]).all() or (diagnostics[..., 3] != 0).any():
        raise ValueError('Unexpected guide gate or readiness claim')
    accepted = np.isin(codes, [0, 7])
    if not accepted.any():
        raise ValueError('No accepted reconstruction guides')
    primary = arrays['DenoiserPrimaryPosition'][..., 3]
    if (not np.isin(primary, [0, 1]).all() or (primary[accepted] != 1).any() or
            not np.array_equal(primary == 0, codes == 4)):
        raise ValueError('Invalid primary guide provenance')
    validity = np.zeros_like(diagnostics)
    validity[..., 0] = accepted
    validity[..., 1] = codes == 0
    validity[..., 2] = codes == 7
    if not np.array_equal(arrays['NrdValidity'], validity):
        raise ValueError('Validity mask does not match conversion disposition')
    normal = arrays['NormalRoughness']
    if ((abs(np.linalg.norm(normal[..., :3][accepted], axis=-1)-1) > 2e-3).any() or
            (normal[..., 3][accepted] < 0).any() or (normal[..., 3][accepted] > 1).any()):
        raise ValueError('Invalid accepted source normal or roughness')
    expected = normal.copy()
    divisor = abs(normal[..., :3]).max(axis=-1, keepdims=True)
    expected[..., :3] = np.divide(normal[..., :3], divisor,
        out=np.zeros_like(normal[..., :3]), where=divisor != 0)
    if not np.allclose(arrays['NrdNormalRoughness'][accepted], expected[accepted], rtol=2e-6, atol=2e-6):
        raise ValueError('SDK normal/roughness encoding mismatch')
    actual_z = arrays['NrdViewZ']
    if (actual_z[..., 1:] != 0).any() or not np.array_equal(
            actual_z[..., 0][accepted], arrays['ViewZ'][..., 0][accepted]):
        raise ValueError('ViewZ copy mismatch')
    if ((actual_z[..., 0][accepted] <= 0).any() or (actual_z[..., 0][accepted] >= 1e6).any() or
            (actual_z[..., 0][~accepted] != 1e6).any() or
            (arrays['NrdNormalRoughness'][~accepted] != 0).any()):
        raise ValueError('Invalid active depth or excluded guide leakage')
    return dict(acceptedPixels=int(accepted.sum()), reconstructionPixels=int((codes == 7).sum()),
        excludedPixels=int((~accepted).sum()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    write_json(output/'scene.json', json.loads((source/'scene.json').read_text()))
    preset_source = source/('preset.json' if (source/'preset.json').exists() else 'on.json')
    write_json(output/'preset.json', json.loads(preset_source.read_text()))
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    report = dict(schemaVersion=1, status='running', inputReady=False, records=[], failures=[],
        exeSha256=sha(exe), shaderSha256=sha(ROOT/'build/shaders/shaders_NrdConversion_CSMain.cso'),
        sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'))
    try:
        buffers = {}
        meta = None
        for name in NAMES:
            buffers[name], record = capture(exe, output, name, output/'preset.json', name)
            report['records'].append(record)
            other = {key: value for key, value in buffers[name][0].items() if key not in ('resource', 'format')}
            if meta is not None and other != meta:
                raise ValueError('Capture cohort mismatch: '+name)
            meta = other
            write_json(output/'report.json', report)
        report.update(validate_guides(buffers), status='passed')
    except Exception as error:
        report.update(status='failed')
        report['failures'].append(str(error))
    report['limitation'] = ('Guide preparation only, pinned normal encoding 4 / linear roughness 1. '
        'No live reconstruction, RELAX dispatch, temporal readiness or final-image quality claim.')
    write_json(output/'report.json', report)
    print(report['status'], report.get('acceptedPixels'), report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
