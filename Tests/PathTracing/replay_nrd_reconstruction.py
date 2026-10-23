"""Replay captured distance/normal/depth through NRD; RGB is a synthetic probe."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer, projection_plane_forward
from validate_part1 import sha, write_json
from audit_nrd_replacement_coverage import surface_labels, assess_plane_normals
from validate_secondary_geometry import planes_from_scene

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('DenoiserPrimaryPosition', 'NormalRoughness', 'ViewZ',
         'DenoiserSpecularDistance', 'DenoiserDiffuseDistance')


def load_cohort(folder):
    report = json.loads((folder/'report.json').read_text())
    if (report.get('status') != 'passed' or not report.get('assessment', {}).get('passed') or
            not report.get('nativeRadianceBitIdentical') or report.get('sceneSha256') != sha(folder/'scene.json')):
        raise ValueError('Requires passing unchanged capture cohort')
    buffers = {}
    for name in NAMES:
        records = [r for r in report['records'] if r.get('resource') == name]
        path = folder/(name+'.ptbuf')
        if len(records) != 1 or records[0].get('d3d12Errors') != 0 or records[0]['sha256'] != sha(path):
            raise ValueError('Missing, duplicate or altered capture: '+name)
        buffers[name] = read_buffer(path)
    meta = buffers[NAMES[0]][0]
    for name, (other, values) in buffers.items():
        expected_format = 10 if name == 'NormalRoughness' else (41 if name == 'ViewZ' else 2)
        if (other['resource'] != 'PathTracing.'+name or other['format'] != expected_format or
                values.shape[:2] != (meta['height'], meta['width']) or
                {k: v for k, v in other.items() if k not in ('resource', 'format')} !=
                {k: v for k, v in meta.items() if k not in ('resource', 'format')}):
            raise ValueError('Capture cohort mismatch: '+name)
    if meta['samplesPerFrame'] != 1 or meta['primarySampling'] != 'pixel-center':
        raise ValueError('Requires coherent single-sample primary inputs')
    scene = json.loads((folder/'scene.json').read_text())
    planes = planes_from_scene(scene)
    labels = surface_labels(buffers[NAMES[0]][1], planes)
    normal_check = assess_plane_normals(buffers['NormalRoughness'][1], labels, planes)
    positions = buffers[NAMES[0]][1]
    primary = positions[..., 3] == 1
    if not np.isin(positions[..., 3], [0, 1]).all():
        raise ValueError('Invalid primary-hit mask')
    expected_viewz = (positions[..., :3]-np.asarray(meta['cameraPosition'])) @ projection_plane_forward(meta['inverseViewProjection'])
    observed = buffers['ViewZ'][1][..., 0]
    error = abs(observed-expected_viewz)
    if ((observed[~primary] != 0).any() or (observed[primary] <= 0).any() or
            (error[primary] > .0002+2e-5*abs(expected_viewz[primary])).any()):
        raise ValueError('ViewZ disagrees with captured primary geometry')
    normal_check['maximumViewZError'] = float(error[primary].max())
    return report, meta, buffers, labels, normal_check


def distance_input(data, primary, ray_max, hypothetical_environment):
    if (data.shape != primary.shape+(4,) or not np.isfinite(data).all() or
            not np.isfinite(ray_max) or ray_max <= 0):
        raise ValueError('Invalid distance array or ray range')
    if (not np.isin(data[..., 1], [0, 1, 2, 3, 4]).all() or (data[..., 3] != 0).any() or
            ((data[..., 1] == 0) & primary).any()):
        raise ValueError('Invalid captured distance states')
    hit = data[..., 1] == 1
    miss = data[..., 1] == 2
    if ((hit & ((data[..., 0] <= 0) | (data[..., 0] > 65504) | (data[..., 2] <= 0) |
                (data[..., 2] > ray_max))).any() or
            (miss & ((data[..., 0] != 0) | (data[..., 2] != ray_max))).any() or
            (~hit & (data[..., 0] != 0)).any() or
            (np.isin(data[..., 1], [0, 3, 4]) & (data[..., 2] != 0)).any() or
            ((~primary) & (data != 0).any(axis=-1)).any()):
        raise ValueError('Contradictory distance capture')
    packed = np.zeros(data.shape, dtype='<f4')
    active = primary & (data[..., 1] != 4)
    packed[active, :3] = [1, 2, 3]
    packed[hit, 3] = data[hit, 0]
    if hypothetical_environment:
        packed[miss, 3] = 65504
    return packed


def assess_output(output, input_values, states, primary, labels, viewz):
    eligible = primary & (states[..., 1] != 4) & (abs(viewz[..., 0]) < 1000)
    missing = eligible & (states[..., 1] == 3)
    written = abs(viewz[..., 0]) < 1000
    if (not np.isfinite(output).all() or (output[..., 3] < 0).any() or
            (output[..., 3] > 65504.5).any() or
            not np.array_equal(output[written, :3], input_values[written, :3]) or
            (output[~written] != 0).any()):
        raise ValueError('Invalid SDK output or RGB/early-out preservation failure')
    unresolved = missing & (output[..., 3] == 0)
    return dict(invalidDirections=int(missing.sum()), reconstructed=int((missing & ~unresolved).sum()),
        unresolved=int(unresolved.sum()), unresolvedUnlabelled=int((unresolved & (labels < 0)).sum()),
        invalidUnlabelled=int((missing & (labels < 0)).sum()),
        allActiveUnresolved=int((eligible & (output[..., 3] == 0)).sum()), rgbPreserved=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.NrdReconstructionTests.exe')
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    source = args.input.resolve(strict=True)
    output = args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be an empty folder separate from source evidence')
    source_report, meta, buffers, labels, normal_check = load_cohort(source)
    primary = buffers[NAMES[0]][1][..., 3] == 1
    viewz = buffers['ViewZ'][1].copy()
    viewz[~primary] = 1e6
    height, width = primary.shape
    report = dict(schemaVersion=1, status='running', inputReady=False, runtimeGeometrySnapshotVerified=False,
        sourceReportSha256=sha(source/'report.json'), exeSha256=sha(args.exe), scriptSha256=sha(Path(__file__)),
        normalAssessment=normal_check, hardware=args.hardware, records=[], failures=[],
        limitation='Captured geometry signals with constant RGB probe, not demodulated lobe radiance or runtime NRD.')
    write_json(output/'report.json', report)
    try:
        for hypothetical in (False, True):
            variant = 'hypothetical-environment' if hypothetical else 'hit-only'
            folder = output/variant
            folder.mkdir(parents=True)
            np.zeros(((height+15)//16, (width+15)//16), dtype='<f4').tofile(folder/'tiles.raw')
            buffers['NormalRoughness'][1].astype('<f4').tofile(folder/'normals.raw')
            viewz.astype('<f4').tofile(folder/'viewz.raw')
            inputs = {}
            for lobe in ('Specular', 'Diffuse'):
                inputs[lobe] = distance_input(buffers['Denoiser'+lobe+'Distance'][1], primary,
                    meta['rayTMax'], hypothetical)
                inputs[lobe].tofile(folder/(lobe.lower()+'.raw'))
            for mode in ('3x3', '5x5'):
                shader = ROOT/'build/Tests/NrdPacking'/('RELAX_Reconstruction_mode'+('0' if mode == '3x3' else '1')+'.cso')
                command = [str(args.exe.resolve()), str(shader), mode, '--replay', str(folder), str(width), str(height)]
                if args.hardware:
                    command.append('--hardware')
                run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
                (folder/(mode+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
                if run.returncode != 0:
                    raise ValueError('SDK replay failed: '+variant+' '+mode+' '+run.stderr)
                record = dict(variant=variant, mode=mode, shaderSha256=sha(shader), d3d12Errors=0,
                    hypotheticalEnvironment=hypothetical, lobes={},
                    inputSha256={name: sha(folder/name) for name in
                        ('tiles.raw', 'normals.raw', 'viewz.raw', 'specular.raw', 'diffuse.raw')})
                for lobe in ('Specular', 'Diffuse'):
                    path = folder/(lobe.lower()+'-'+mode+'.raw')
                    values = np.fromfile(path, dtype='<f4').reshape(height, width, 4)
                    record['lobes'][lobe] = assess_output(values, inputs[lobe],
                        buffers['Denoiser'+lobe+'Distance'][1], primary, labels, viewz)
                    record['lobes'][lobe]['outputSha256'] = sha(path)
                report['records'].append(record)
                write_json(output/'report.json', report)
                print(json.dumps(record), flush=True)
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
