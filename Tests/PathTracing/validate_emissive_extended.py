"""Validate unequal, textured, small and large emitters against area quadrature."""
import argparse
import base64
import copy
import io
import json
from pathlib import Path
import struct
import subprocess
from types import SimpleNamespace

import numpy as np
from PIL import Image
from compare_hdr import capture
from validate_emissive import ROOT, ROI, fixture, reference
from validate_part1 import sha, write_json
from validate_transport import reference_agreement, paired_agreement


def emitters_for(case):
    if case == 'unequal':
        return [dict(half_size=1.0, height=3.0, center=(-2, 0), emission=[.8, .4, .2]),
            dict(half_size=.5, height=3.0, center=(2, 0), emission=[.2, .6, .9])]
    if case == 'texture':
        return [dict(half_size=1.5, height=3.0, center=(0, 0), emission=[.8, .4, .2], texture=True)]
    if case == 'small':
        return [dict(half_size=.15, height=3.0, center=(0, 0), emission=[.8, .4, .2])]
    if case == 'large':
        return [dict(half_size=4.5, height=3.0, center=(0, 0), emission=[.8, .4, .2])]
    raise ValueError('Unknown emitter case')


def make_fixture(output, case):
    scene, preset = fixture(output)
    source = json.loads((output/'emitter.gltf').read_text())
    scene['assets'] = []
    scene['nodes'] = scene['nodes'][:1]
    emitters = emitters_for(case)
    for index, emitter in enumerate(emitters):
        gltf = copy.deepcopy(source)
        gltf['materials'][0]['emissiveFactor'] = emitter['emission']
        if emitter.get('texture'):
            raw = base64.b64decode(gltf['buffers'][0]['uri'].split(',')[1])
            uv = struct.pack('<8f', 0, 0, 1, 0, 1, 1, 0, 1)
            raw = raw[:96]+uv+raw[96:]
            gltf['buffers'][0].update(byteLength=len(raw),
                uri='data:application/octet-stream;base64,'+base64.b64encode(raw).decode())
            gltf['bufferViews'][2]['byteOffset'] = 128
            gltf['bufferViews'].append(dict(buffer=0, byteOffset=96, byteLength=32))
            gltf['accessors'].append(dict(bufferView=3, componentType=5126, count=4, type='VEC2'))
            gltf['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0'] = 3
            image = Image.new('RGBA', (2, 2))
            image.putdata([(0, 0, 0, 255), (255, 255, 255, 255),
                (255, 255, 255, 255), (0, 0, 0, 255)])
            stream = io.BytesIO()
            image.save(stream, format='PNG')
            gltf['images'] = [dict(uri='data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode())]
            gltf['textures'] = [dict(source=0)]
            gltf['materials'][0]['emissiveTexture'] = dict(index=0, texCoord=0)
        name = f'emitter-{index}'
        write_json(output/(name+'.gltf'), gltf)
        scene['assets'].append(dict(id=name, type='gltf', path=name+'.gltf'))
        size = emitter['half_size']/1.5
        scene['nodes'].append(dict(id=name, name=name, type='gltf', assetId=name, parentId=None,
            translation=[emitter['center'][0], 0, emitter['center'][1]], rotation=[0, 0, 0, 1],
            scale=[size, 1, size], visible=True))
    scene.update(sceneId='emissive-'+case, name='PT Emissive '+case)
    preset['renderingPath'] = 2
    preset['pathTracing'].update(emissiveSamplingMode=1, environmentSamplingMode=0)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    return scene, preset, emitters


def run_case(args, output, case):
    output.mkdir()
    scene, preset, emitters = make_fixture(output, case)
    oracle = reference(scene, emitters)
    report = dict(case=case, reference=oracle, records=[], agreements={}, channelAgreements={}, pairedAgreements={})
    request = SimpleNamespace(root=ROOT, exe=args.exe, scene_file=output/'scene.json',
        render_preset=output/'preset.json', output=output, timeout=300, roi=ROI)
    means_by_mode = {}
    for mode in [0, 1, 2]:
        preset['pathTracing']['emissiveSamplingMode'] = mode
        write_json(output/'preset.json', preset)
        means = []
        channels = []
        for seed in args.seeds:
            record, pixels = capture(request, 0, seed, args.samples, f'mode-{mode}-seed-{seed}')
            diagnostic = record['diagnostics']
            if (diagnostic.get('emissiveTriangleCount') != 2*len(emitters) or
                    diagnostic.get('emissiveTableStatus') != 'ready' or
                    diagnostic.get('emissiveSamplingMode') != mode or not diagnostic['emissiveEnabled'] or
                    diagnostic['environmentEnabled'] or diagnostic['directLightingEnabled'] or
                    diagnostic['maxBounces'] != 2 or tuple(record['dimensions']) != (1920, 1080)):
                raise ValueError('Unexpected integration settings or emitter table')
            rgb = np.asarray(pixels).reshape(-1, 3).mean(axis=0)
            mean = float(rgb.mean())
            record.update(mean=mean, rgb=rgb.tolist(), emissiveSamplingMode=mode,
                presetSha256=sha(output/'preset.json'))
            means.append(mean)
            channels.append(rgb)
            report['records'].append(record)
            write_json(output/'report.json', report)
        means_by_mode[mode] = means
        report['agreements'][str(mode)] = reference_agreement(means, oracle)
        report['channelAgreements'][str(mode)] = [reference_agreement(
            [float(rgb[channel]) for rgb in channels], dict(oracle, mean=oracle['rgb'][channel]))
            for channel in range(3)]
    report['pairedAgreements'] = {str(mode): paired_agreement(means_by_mode[0], means_by_mode[mode])
        for mode in [1, 2]}
    preset['pathTracing']['emissiveEnabled'] = False
    write_json(output/'preset.json', preset)
    record, pixels = capture(request, 0, args.seeds[0], args.samples, 'emission-off')
    report['offControl'] = dict(capture=record, maximum=float(max(abs(v) for v in pixels)))
    statuses = [entry['status'] for entry in report['agreements'].values()]+[
        entry['status'] for channels in report['channelAgreements'].values() for entry in channels]+[
        entry['status'] for entry in report['pairedAgreements'].values()]
    report['status'] = 'passed' if all(status == 'passed' for status in statuses) else (
        'failed' if 'failed' in statuses else 'inconclusive')
    if report['offControl']['maximum'] >= 1e-7 or record['diagnostics']['emissiveEnabled']:
        report['status'] = 'failed'
    preset['pathTracing'].update(emissiveEnabled=True, emissiveSamplingMode=1)
    write_json(output/'preset.json', preset)
    write_json(output/'report.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--samples', type=int, default=64)
    parser.add_argument('--seeds', type=int, nargs='+', default=[11, 23, 37, 53])
    parser.add_argument('--cases', nargs='+', choices=['unequal', 'texture', 'small', 'large'],
        default=['unequal', 'texture'])
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or len(set(args.seeds)) < 4 or len(set(args.seeds)) != len(args.seeds):
        parser.error('Require 1-4096 samples and at least four unique seeds')
    if any(seed < 0 or seed > 0xffffffff for seed in args.seeds):
        parser.error('Seeds must be uint32')
    args.exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(schemaVersion=1, status='running', records=[], samples=args.samples, seeds=args.seeds,
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(args.exe), executable=str(args.exe),
        sourceSha256={path: sha(ROOT/path) for path in ['Shaders/shaders_PathTracing.hlsl',
            'Shaders/EmissiveTriangleSampling.hlsli', 'Tests/PathTracing/validate_emissive.py',
            'Tests/PathTracing/validate_emissive_extended.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip())
    try:
        for case in dict.fromkeys(args.cases):
            result = run_case(args, output/case, case)
            report['records'].append(result)
            write_json(output/'report.json', report)
        statuses = [case['status'] for case in report['records']]
        report['status'] = 'passed' if all(status == 'passed' for status in statuses) else (
            'failed' if 'failed' in statuses else 'inconclusive')
    except Exception as error:
        report.update(status='failed', failure=str(error))
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failure=report.get('failure'),
        cases=[dict(case=case['case'], status=case['status'], agreements=case['agreements'])
            for case in report['records']])), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
