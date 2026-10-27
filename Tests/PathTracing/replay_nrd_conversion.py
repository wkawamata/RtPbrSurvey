"""Validate SDK GPU conversion of captured physical lobes; no runtime denoising."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_secondary_geometry import primary_rays
from replay_nrd_reconstruction import load_cohort, distance_input, ROOT


def load_materials(folder, source, meta, buffers):
    for name in ('Albedo', 'MaterialF0Metallic', 'PhysicalDiffuse', 'PhysicalSpecular'):
        records = [r for r in source['records'] if r.get('resource') == name]
        path = folder/(name+'.ptbuf')
        if len(records) != 1 or records[0].get('d3d12Errors') != 0 or records[0]['sha256'] != sha(path):
            raise ValueError('Missing/altered material capture: '+name)
        other, values = read_buffer(path)
        if (other['resource'] != 'PathTracing.'+name or other['format'] != (2 if name.startswith('Physical') else 10) or
                values.shape != (meta['height'], meta['width'], 4) or
                {k: v for k, v in other.items() if k not in ('resource', 'format')} !=
                {k: v for k, v in meta.items() if k not in ('resource', 'format')}):
            raise ValueError('Material cohort mismatch: '+name)
        if name.startswith('Physical') and (values[..., 3] != 0).any():
            raise ValueError('Nonzero reserved physical signal channel')
        buffers[name] = other, values
    return buffers


def make_inputs(meta, buffers, hypothetical):
    height, width = meta['height'], meta['width']
    inputs = np.zeros((height, width, 7, 4), dtype='<f4')
    inputs[..., 0, :] = buffers['NormalRoughness'][1]
    ys, xs = np.mgrid[:height, :width]
    _, directions = primary_rays(meta, xs.ravel(), ys.ravel())
    inputs[..., 1, :3] = -directions.reshape(height, width, 3)
    inputs[..., 1, 3] = buffers['MaterialF0Metallic'][1][..., 3]
    inputs[..., 2, :] = buffers['Albedo'][1]
    inputs[..., 3, :] = buffers['MaterialF0Metallic'][1]
    inputs[..., 4, :] = buffers['PhysicalDiffuse'][1]
    inputs[..., 5, :] = buffers['PhysicalSpecular'][1]
    primary = buffers['DenoiserPrimaryPosition'][1][..., 3] == 1
    if not np.array_equal(buffers['Albedo'][1][..., 3], primary.astype(float)):
        raise ValueError('Primary-hit masks disagree')
    inputs[..., 6, 2] = primary
    for index, lobe in enumerate(('Diffuse', 'Specular')):
        packed = distance_input(buffers['Denoiser'+lobe+'Distance'][1], primary, meta['rayTMax'], hypothetical)
        inputs[..., 6, index] = packed[..., 3]
    return inputs


def assess_conversion(inputs, output):
    if output.shape != inputs.shape[:2]+(4, 4) or not np.isfinite(output).all():
        raise ValueError('Invalid conversion output')
    primary = inputs[..., 6, 2] == 1
    diffuse_status, specular_status = output[..., 2, 3], output[..., 3, 3]
    if (not np.isin(diffuse_status, [0, 1, 2, 3, 4]).all() or
            not np.isin(specular_status, [0, 1, 2, 3, 4]).all() or
            (diffuse_status[primary] == 4).any() or np.isin(specular_status[primary], [3, 4]).any() or
            (diffuse_status[~primary] != 4).any() or (specular_status[~primary] != 4).any() or
            (output[~primary, :, :3] != 0).any() or (output[~primary, :2, 3] != 0).any()):
        raise ValueError('Invalid excluded/status contract')
    inactive_diffuse = (inputs[..., 1, 3] == 1) | ~np.any(inputs[..., 2, :3] > 0, axis=-1)
    if ((diffuse_status == 3) & primary & ~inactive_diffuse).any():
        raise ValueError('Active diffuse lobe was incorrectly excluded')
    accepted = primary & np.isin(diffuse_status, [0, 3]) & (specular_status == 0)
    rejected = primary & ~accepted
    if not accepted.any():
        raise ValueError('No accepted primary pixels; conversion is not validated')
    if (output[rejected, :2, :] != 0).any():
        raise ValueError('Rejected pixel leaked a partial packed signal')
    result = dict(primaryPixels=int(primary.sum()), acceptedPixels=int(accepted.sum()),
        rejectedPixels=int(rejected.sum()), inputReady=False, lobes={})
    for index, lobe in enumerate(('Diffuse', 'Specular')):
        factor = output[..., index+2, :3]
        packed = output[..., index, :]
        physical = inputs[..., index+4, :3]
        if ((factor[accepted] < .01999).any() or (factor[accepted] > 1).any() or
                (packed[accepted] < 0).any() or (packed[accepted] > 65504).any()):
            raise ValueError('Invalid accepted factor or packed value')
        error = abs(packed[..., :3].astype(float)*factor.astype(float)-physical.astype(float))
        if ((error[accepted] > 2e-5+2e-5*abs(physical[accepted])).any() or
                not np.array_equal(packed[accepted, 3], inputs[accepted, 6, index])):
            raise ValueError('Physical RGB round trip or distance preservation failed')
        result['lobes'][lobe] = dict(maximumRoundTripError=float(error[accepted].max()) if accepted.any() else None,
            statusCounts={str(code): int(((diffuse_status if index == 0 else specular_status) == code).sum())
                for code in range(5)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    folder, destination = args.input.resolve(strict=True), args.output.resolve()
    if destination == folder or folder in destination.parents or (destination.exists() and any(destination.iterdir())):
        parser.error('Output must be a separate empty directory')
    source, meta, buffers, _, normal_check = load_cohort(folder)
    load_materials(folder, source, meta, buffers)
    exe = ROOT/'build/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
    shader = ROOT/'build/Tests/NrdPacking/shaders_NrdCapturedConversion_CSMain.cso'
    report = dict(schemaVersion=1, status='running', inputReady=False, runtimeGeometrySnapshotVerified=False,
        sourceReportSha256=sha(folder/'report.json'), exeSha256=sha(exe), shaderSha256=sha(shader),
        scriptSha256=sha(Path(__file__)), normalAssessment=normal_check, hardware=args.hardware, records=[], failures=[])
    write_json(destination/'report.json', report)
    try:
        for hypothetical in (False, True):
            variant = 'hypothetical-environment' if hypothetical else 'hit-only'
            inputs = make_inputs(meta, buffers, hypothetical)
            input_path = destination/(variant+'-input.raw')
            output_path = destination/(variant+'-output.raw')
            inputs.tofile(input_path)
            command = [str(exe), str(shader), '--input-conversion', '--input', str(input_path), '--output', str(output_path)]
            if args.hardware:
                command.append('--hardware')
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
            (destination/(variant+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
            if run.returncode != 0:
                raise ValueError('GPU conversion failed: '+run.stderr)
            converted = np.fromfile(output_path, dtype='<f4').reshape(meta['height'], meta['width'], 4, 4)
            record = dict(variant=variant, hypotheticalEnvironment=hypothetical, d3d12Errors=0,
                inputSha256=sha(input_path), outputSha256=sha(output_path), assessment=assess_conversion(inputs, converted))
            report['records'].append(record)
            write_json(destination/'report.json', report)
            print(json.dumps(record), flush=True)
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    report['limitation'] = ('Captured lobe/material arithmetic conversion only. Environment encoding is hypothetical; '
        'no runtime snapshot proof, reconstruction chaining, history or denoiser quality approval.')
    write_json(destination/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
