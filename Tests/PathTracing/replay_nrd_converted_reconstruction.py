"""Chain captured physical-lobe conversion through isolated SDK reconstruction."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from replay_nrd_conversion import load_materials, make_inputs, assess_conversion
from replay_nrd_reconstruction import ROOT, load_cohort, assess_output
from validate_part1 import sha, write_json


VARIANTS = ('hit-only', 'hypothetical-environment')


def validate_report(report, source_hash):
    records = report.get('records', [])
    if (report.get('status') != 'passed' or report.get('failures') != [] or
            report.get('sourceReportSha256') != source_hash or report.get('inputReady') is not False or
            report.get('runtimeGeometrySnapshotVerified') is not False or len(records) != 2 or
            sorted(r.get('variant', '') for r in records) != sorted(VARIANTS)):
        raise ValueError('Incomplete or mismatched conversion provenance')
    for record in records:
        if (record.get('d3d12Errors') != 0 or
                record.get('hypotheticalEnvironment') != (record['variant'] == VARIANTS[1])):
            raise ValueError('Invalid conversion record')
    return {r['variant']: r for r in records}


def assess_chain(values, packed, factors, physical, states, primary, labels, viewz):
    result = assess_output(values, packed, states, primary, labels, viewz)
    written = primary & (abs(viewz[..., 0]) < 1000)
    if not written.any():
        raise ValueError('No reconstructed primary pixels')
    error = abs(values[..., :3].astype(float)*factors.astype(float)-physical.astype(float))
    if (error[written] > 2e-5+2e-5*abs(physical[written])).any():
        raise ValueError('Reconstructed physical RGB round trip failed')
    result['maximumPhysicalRoundTripError'] = float(error[written].max())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--conversion', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    source, conversion = args.input.resolve(strict=True), args.conversion.resolve(strict=True)
    destination = args.output.resolve()
    if (any(destination == p or p in destination.parents for p in (source, conversion)) or
            (destination.exists() and any(destination.iterdir()))):
        parser.error('Output must be a separate empty directory')
    source_report, meta, buffers, labels, normal_check = load_cohort(source)
    load_materials(source, source_report, meta, buffers)
    conversion_report = json.loads((conversion/'report.json').read_text())
    records = validate_report(conversion_report, sha(source/'report.json'))
    primary = buffers['DenoiserPrimaryPosition'][1][..., 3] == 1
    viewz = buffers['ViewZ'][1].copy()
    viewz[~primary] = 1e6
    height, width = primary.shape
    exe = ROOT/'build/Debug/RtPbrSurvey.NrdReconstructionTests.exe'
    report = dict(schemaVersion=1, status='running', inputReady=False,
        runtimeGeometrySnapshotVerified=False, hardware=args.hardware,
        conversionHardware=conversion_report.get('hardware'), normalAssessment=normal_check,
        sourceReportSha256=sha(source/'report.json'), conversionReportSha256=sha(conversion/'report.json'),
        exeSha256=sha(exe), scriptSha256=sha(Path(__file__)), records=[], failures=[])
    write_json(destination/'report.json', report)
    try:
        for variant in VARIANTS:
            record = records[variant]
            input_path, converted_path = conversion/(variant+'-input.raw'), conversion/(variant+'-output.raw')
            if record.get('inputSha256') != sha(input_path) or record.get('outputSha256') != sha(converted_path):
                raise ValueError('Altered conversion evidence')
            inputs = np.fromfile(input_path, dtype='<f4').reshape(height, width, 7, 4)
            expected = make_inputs(meta, buffers, variant == VARIANTS[1])
            if not np.array_equal(inputs, expected):
                raise ValueError('Conversion inputs do not match captured cohort')
            converted = np.fromfile(converted_path, dtype='<f4').reshape(height, width, 4, 4)
            assessment = assess_conversion(inputs, converted)
            if assessment != record.get('assessment') or assessment['rejectedPixels'] != 0:
                raise ValueError('Incomplete or inconsistent conversion assessment')
            folder = destination/variant
            folder.mkdir()
            np.zeros(((height+15)//16, (width+15)//16), dtype='<f4').tofile(folder/'tiles.raw')
            buffers['NormalRoughness'][1].astype('<f4').tofile(folder/'normals.raw')
            viewz.astype('<f4').tofile(folder/'viewz.raw')
            for index, lobe in enumerate(('Diffuse', 'Specular')):
                converted[..., index, :].copy().tofile(folder/(lobe.lower()+'.raw'))
            for mode, shader_index in (('3x3', 0), ('5x5', 1)):
                shader = ROOT/'build/Tests/NrdPacking'/f'RELAX_Reconstruction_mode{shader_index}.cso'
                command = [str(exe), str(shader), mode, '--replay', str(folder), str(width), str(height)]
                if args.hardware:
                    command.append('--hardware')
                run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
                (folder/(mode+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
                if run.returncode != 0:
                    raise ValueError('SDK reconstruction failed: '+run.stderr)
                result = dict(variant=variant, mode=mode, d3d12Errors=0, shaderSha256=sha(shader),
                    convertedOutputSha256=sha(converted_path), lobes={}, inputSha256={
                        name: sha(folder/name) for name in
                        ('tiles.raw', 'normals.raw', 'viewz.raw', 'diffuse.raw', 'specular.raw')})
                for index, lobe in enumerate(('Diffuse', 'Specular')):
                    path = folder/(lobe.lower()+'-'+mode+'.raw')
                    values = np.fromfile(path, dtype='<f4').reshape(height, width, 4)
                    result['lobes'][lobe] = assess_chain(values, converted[..., index, :],
                        converted[..., index+2, :3], inputs[..., index+4, :3],
                        buffers['Denoiser'+lobe+'Distance'][1], primary, labels, viewz)
                    result['lobes'][lobe]['outputSha256'] = sha(path)
                report['records'].append(result)
                write_json(destination/'report.json', report)
                print(json.dumps(result), flush=True)
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    report['limitation'] = ('Isolated conversion/reconstruction chain only; environment distances are hypothetical. '
        'No live geometry snapshot proof, full RELAX denoising, history or visual quality approval.')
    write_json(destination/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
