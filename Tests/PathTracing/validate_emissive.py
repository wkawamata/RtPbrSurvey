"""Compare emissive estimators with an independent area-light reference and visibility controls."""
import argparse
import base64
import copy
import json
import math
from pathlib import Path
import statistics
import struct
import subprocess
from types import SimpleNamespace

import numpy as np
from compare_hdr import capture
from validate_part1 import sha, write_json
from validate_inputs import srgb_texture_material
from validate_transport import reference_agreement

ROOT = Path(__file__).resolve().parents[2]
EMISSION = np.array([.8, .4, .2])
HALF_SIZE = 1.5
HEIGHT = 3.0
ROI = [928, 508, 64, 64]


def rectangle_integral(position, view, albedo, roughness, order=64):
    nodes, weights = np.polynomial.legendre.leggauss(order)
    x, z = np.meshgrid(nodes*HALF_SIZE, nodes*HALF_SIZE)
    points = np.stack((x, np.full_like(x, HEIGHT), z), axis=-1)
    delta = points-np.asarray(position)
    distance_squared = np.sum(delta*delta, axis=-1)
    light = delta/np.sqrt(distance_squared)[..., None]
    mu = light[..., 1]
    view = np.asarray(view, dtype=float)
    view /= np.linalg.norm(view)
    if view[1] <= 0 or np.any(mu <= 0):
        raise ValueError('Receiver/view must face the emitter')
    half = light+view
    half /= np.linalg.norm(half, axis=-1)[..., None]
    nh = half[..., 1]
    vh = np.sum(half*view, axis=-1)
    alpha_squared = max(roughness**2, .001)**2
    distribution = alpha_squared/(math.pi*(nh*nh*(alpha_squared-1)+1)**2)
    def smith(cosine):
        return 2*cosine/(cosine+np.sqrt(alpha_squared+(1-alpha_squared)*cosine*cosine))
    fresnel = .04+.96*(1-vh)**5
    brdf = (1-fresnel)*albedo/math.pi + distribution*smith(view[1])*smith(mu)*fresnel/(4*view[1]*mu)
    area_weights = np.outer(weights, weights)*HALF_SIZE**2
    scalar = float(np.sum(brdf*mu*mu/distance_squared*area_weights))
    return EMISSION*scalar


def reference(scene):
    camera = scene['camera']
    eye = np.asarray(camera['position'], dtype=float)
    forward = np.asarray(camera['target'])-eye
    forward /= np.linalg.norm(forward)
    right = np.cross(camera['up'], forward)
    right /= np.linalg.norm(right)
    up = np.cross(forward, right)
    albedo = srgb_texture_material([.5]*3, .8)[0][0]
    results = {64: [], 128: []}
    for y in [518.5, 539.5, 560.5]:
        for x in [938.5, 959.5, 980.5]:
            tangent = math.tan(math.radians(camera['verticalFovDegrees'])/2)
            direction = forward+right*(2*x/1920-1)*tangent*1920/1080+up*(1-2*y/1080)*tangent
            direction /= np.linalg.norm(direction)
            position = eye+direction*(-eye[1]/direction[1])
            for order in results:
                results[order].append(rectangle_integral(position, -direction, albedo, .8, order))
    low, high = [np.mean(results[order], axis=0) for order in [64, 128]]
    return dict(mean=float(high.mean()), rgb=high.tolist(), orders=[64, 128], spatialSamples=9,
        quadratureRelativeChange=float(np.max(np.abs(low/high-1))),
        limitation='Nine representative ROI positions; opaque one-sided constant emitter, two-bounce path limit.')


def fixture(output):
    folder = ROOT/'Assets/Scenes/PathTracingValidation/constant-environment'
    scene = json.loads((folder/'scene.json').read_text())
    preset = json.loads((folder/'render-preset.json').read_text())
    positions = [(-HALF_SIZE, HEIGHT, -HALF_SIZE), (HALF_SIZE, HEIGHT, -HALF_SIZE),
        (HALF_SIZE, HEIGHT, HALF_SIZE), (-HALF_SIZE, HEIGHT, HALF_SIZE)]
    raw = b''.join(struct.pack('<3f', *p) for p in positions)
    raw += b''.join(struct.pack('<3f', 0, -1, 0) for _ in positions)
    raw += struct.pack('<6H', 0, 1, 2, 0, 2, 3)
    gltf = dict(asset=dict(version='2.0'), buffers=[dict(byteLength=len(raw),
        uri='data:application/octet-stream;base64,'+base64.b64encode(raw).decode())],
        bufferViews=[dict(buffer=0, byteOffset=0, byteLength=48),
            dict(buffer=0, byteOffset=48, byteLength=48), dict(buffer=0, byteOffset=96, byteLength=12)],
        accessors=[dict(bufferView=0, componentType=5126, count=4, type='VEC3',
            min=[-HALF_SIZE, HEIGHT, -HALF_SIZE], max=[HALF_SIZE, HEIGHT, HALF_SIZE]),
            dict(bufferView=1, componentType=5126, count=4, type='VEC3'),
            dict(bufferView=2, componentType=5123, count=6, type='SCALAR')],
        materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=[0, 0, 0, 1],
            metallicFactor=0, roughnessFactor=1), emissiveFactor=EMISSION.tolist())],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1), indices=2, material=0)])],
        nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0)
    write_json(output/'emitter.gltf', gltf)
    scene['assets'] = [dict(id='emitter', type='gltf', path='emitter.gltf')]
    scene['nodes'].append(dict(id='emitter', name='emitter', type='gltf', assetId='emitter', parentId=None,
        translation=[0, 0, 0], rotation=[0, 0, 0, 1], scale=[1, 1, 1], visible=True))
    scene.update(sceneId='emissive-baseline', renderPreset='preset.json')
    preset['lighting'].update(lights=[], skyboxEnabled=False, diffuseIblEnabled=False,
        specularIblEnabled=False, emissiveEnabled=True)
    preset['pathTracing'].update(maxBounces=2, debugOutput=3, directLightingEnabled=False,
        environmentEnabled=False, emissiveEnabled=True, russianRouletteEnabled=False)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    return scene, preset


def blocked_scene(scene):
    result = copy.deepcopy(scene)
    result['nodes'].append(dict(id='blocker', name='blocker', type='primitive', parentId=None,
        translation=[0, 1.5, 0], rotation=[0, 0, 0, 1], scale=[4, .1, 4], visible=True,
        primitive=dict(kind='cube', size=1), materialId='dielectric'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=64)
    parser.add_argument('--require-emitter-table', action='store_true')
    parser.add_argument('--modes', type=int, nargs='+', choices=[0, 1], default=[0])
    parser.add_argument('--visibility-controls', action='store_true')
    parser.add_argument('--seeds', type=int, nargs='+', default=[11, 23, 37, 53])
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or len(set(args.seeds)) < 4 or len(set(args.seeds)) != len(args.seeds):
        parser.error('Require positive samples and at least four unique seeds')
    if any(s < 0 or s > 0xffffffff for s in args.seeds):
        parser.error('Seeds must be uint32')
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    scene, preset = fixture(output)
    oracle = reference(scene)
    report = dict(schemaVersion=1, status='running', reference=oracle, records=[],
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'), samples=args.samples, seeds=args.seeds,
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Shaders/PathTracingSampling.hlsli', 'App/RtPbrSurveyApp.cpp',
            'Tests/PathTracing/validate_emissive.py']},
        sceneSha256=sha(output/'scene.json'), emitterSha256=sha(output/'emitter.gltf'),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip())
    request = SimpleNamespace(root=ROOT, exe=ROOT/'bin/x64/Debug/RtPbrSurvey.exe',
        scene_file=output/'scene.json', render_preset=output/'preset.json', output=output, timeout=300, roi=ROI)
    try:
        agreements = {}
        for mode in dict.fromkeys(args.modes):
            means = []
            preset['pathTracing']['emissiveSamplingMode'] = mode
            write_json(output/'preset.json', preset)
            for seed in args.seeds:
                record, pixels = capture(request, 0, seed, args.samples, f'mode-{mode}-seed-{seed}')
                diagnostic = record['diagnostics']
                if args.require_emitter_table and (diagnostic.get('emissiveTriangleCount') != 2 or
                        diagnostic.get('emissiveTableStatus') != 'ready'):
                    raise ValueError('Expected two uploaded emitter triangles')
                if (not diagnostic['emissiveEnabled'] or diagnostic['environmentEnabled'] or
                        diagnostic['directLightingEnabled'] or diagnostic['maxBounces'] != 2 or
                        diagnostic.get('emissiveSamplingMode') != mode):
                    raise ValueError('Wrong lighting settings')
                mean = statistics.mean(pixels)
                record.update(mean=mean, emissiveSamplingMode=mode, presetSha256=sha(output/'preset.json'))
                means.append(mean)
                report['records'].append(record)
                write_json(output/'report.json', report)
            agreements[str(mode)] = reference_agreement(means, oracle)
        report['agreements'] = agreements
        report['agreement'] = agreements[str(args.modes[0])]
        zero_controls = []
        if args.visibility_controls:
            write_json(output/'scene.json', blocked_scene(scene))
            report['blockedControls'] = []
            for mode in dict.fromkeys(args.modes):
                preset['pathTracing']['emissiveSamplingMode'] = mode
                write_json(output/'preset.json', preset)
                record, pixels = capture(request, 0, args.seeds[0], args.samples, f'blocked-mode-{mode}')
                report['blockedControls'].append(dict(capture=record, maximum=float(max(abs(v) for v in pixels))))
            zero_controls.extend(report['blockedControls'])
            write_json(output/'scene.json', scene)
            preset['pathTracing'].update(maxBounces=1, emissiveSamplingMode=1)
            write_json(output/'preset.json', preset)
            record, pixels = capture(request, 0, args.seeds[0], args.samples, 'one-bounce')
            report['oneBounceControl'] = dict(capture=record, maximum=float(max(abs(v) for v in pixels)))
            zero_controls.append(report['oneBounceControl'])
            primary_scene = copy.deepcopy(scene)
            primary_scene['camera'].update(position=[0, .1, 0], target=[0, HEIGHT, 0], up=[0, 0, 1])
            write_json(output/'scene.json', primary_scene)
            report['primaryControls'] = []
            for mode in dict.fromkeys(args.modes):
                preset['pathTracing']['emissiveSamplingMode'] = mode
                write_json(output/'preset.json', preset)
                record, pixels = capture(request, 0, args.seeds[0], args.samples, f'primary-mode-{mode}')
                maximum_error = float(np.max(np.abs(np.asarray(pixels).reshape(-1, 3) - EMISSION)))
                report['primaryControls'].append(dict(capture=record, maximumError=maximum_error))
            write_json(output/'scene.json', scene)
            preset['pathTracing']['maxBounces'] = 2
        preset['pathTracing']['emissiveEnabled'] = False
        write_json(output/'preset.json', preset)
        record, pixels = capture(request, 0, args.seeds[0], args.samples, 'emission-off')
        if record['diagnostics']['emissiveEnabled']:
            raise ValueError('Emission-off control was not disabled')
        report['offControl'] = dict(capture=record, maximum=float(max(abs(v) for v in pixels)))
        controls = [report['offControl']] + zero_controls
        statuses = [value['status'] for value in agreements.values()]
        report['status'] = ('passed' if all(status == 'passed' for status in statuses) else
            'failed' if 'failed' in statuses else 'inconclusive')
        if any(control['maximum'] >= 1e-7 for control in controls):
            report['status'] = 'failed'
        if any(control['maximumError'] >= 1e-5 for control in report.get('primaryControls', [])):
            report['status'] = 'failed'
    except Exception as error:
        report.update(status='failed', failure=str(error))
    write_json(output/'report.json', report)
    print(json.dumps({k: v for k, v in report.items() if k in ['status', 'agreement', 'failure']}), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
