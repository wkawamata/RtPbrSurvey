"""Capture one static PT production, convert actual-continuation inputs, and call native RR."""
import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import zlib
import shutil

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_rr_texture_conversion import reference

ROOT = Path(__file__).resolve().parents[2]
NAMES = ('SecondaryRayData', 'PrimaryContinuation', 'SecondarySurfaceDistance',
         'CurrentFrameRadiance', 'NormalRoughness', 'ViewZ', 'MotionVectors',
         'Albedo', 'MaterialF0Metallic', 'DenoiserPrimaryPosition')


def fixture(source, output):
    scene = json.loads((source/'scene.json').read_text())
    preset = json.loads((source/'preset.json').read_text())
    scene.update(sceneId='pt-rr-closed-box', name='PT RR closed box', renderPreset='preset.json')
    scene['camera'].update(position=[0, 0, 0], target=[0, 0, 1], verticalFovDegrees=35, nearZ=.1, farZ=100)
    # glTF carries emissive materials without extending the scene JSON schema.
    binary, views, accessors, meshes = bytearray(), [], [], []
    def accessor(values, kind, component):
        data = np.asarray(values, dtype='<f4' if component == 5126 else '<u2')
        offset = len(binary)
        binary.extend(data.tobytes())
        while len(binary) % 4:
            binary.append(0)
        views.append(dict(buffer=0, byteOffset=offset, byteLength=data.nbytes))
        item = dict(bufferView=len(views)-1, componentType=component, count=len(data), type=kind)
        if kind == 'VEC3':
            item.update(min=data.min(axis=0).tolist(), max=data.max(axis=0).tolist())
        accessors.append(item)
        return len(accessors)-1
    for center, normal in (((0, 0, 2), (0, 0, -1)), ((0, 0, -2), (0, 0, 1)),
                           ((0, -2, 0), (0, 1, 0)), ((0, 2, 0), (0, -1, 0)),
                           ((-2, 0, 0), (1, 0, 0)), ((2, 0, 0), (-1, 0, 0))):
        n = np.asarray(normal, dtype=float)
        tangent = np.cross(n, [0, 1, 0] if abs(n[1]) < .9 else [1, 0, 0])
        tangent /= np.linalg.norm(tangent)
        bitangent = np.cross(n, tangent)
        positions = [np.asarray(center)+2*(x*tangent+y*bitangent) for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        attributes = dict(POSITION=accessor(positions, 'VEC3', 5126),
                          NORMAL=accessor([n]*4, 'VEC3', 5126),
                          TEXCOORD_0=accessor([[0, 0], [1, 0], [1, 1], [0, 1]], 'VEC2', 5126))
        meshes.append(dict(primitives=[dict(attributes=attributes, indices=accessor([0, 1, 2, 0, 2, 3], 'SCALAR', 5123),
                                            material=0 if len(meshes) == 1 else 1)]))
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))
    white = (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))+
             chunk(b'IDAT', zlib.compress(b'\x00\xff\xff\xff\xff'))+chunk(b'IEND', b''))
    gltf = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=list(range(6)))],
                nodes=[dict(mesh=i) for i in range(6)], meshes=meshes, bufferViews=views, accessors=accessors,
                buffers=[dict(byteLength=len(binary), uri='data:application/octet-stream;base64,'+
                              base64.b64encode(binary).decode())],
                images=[dict(uri='data:image/png;base64,'+base64.b64encode(white).decode())],
                textures=[dict(source=0)],
                materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=[.7, .5, .3, 1], metallicFactor=1, roughnessFactor=0,
                                                        metallicRoughnessTexture=dict(index=0))),
                           dict(pbrMetallicRoughness=dict(baseColorFactor=[.5, .5, .5, 1], metallicFactor=0, roughnessFactor=.5),
                                emissiveFactor=[1, .5, .25], emissiveTexture=dict(index=0))])
    write_json(output/'box.gltf', gltf)
    scene['assets'] = [dict(id='box', type='gltf', path='box.gltf')]
    scene['materials'] = []
    scene['nodes'] = [dict(id='box', name='box', parentId=None, type='gltf', assetId='box',
                           translation=[0, 0, 0], rotation=[0, 0, 0, 1], scale=[1, 1, 1], visible=True)]
    preset['pathTracing'].update(accumulate=False, samplesPerFrame=1, maxBounces=2, randomSeed=7,
                                 directLightingEnabled=False, emissiveEnabled=True, emissiveSamplingMode=0,
                                 environmentEnabled=False, russianRouletteEnabled=False,
                                 pixelCenterPrimary=True, denoiserDistanceDiagnostics=True)
    preset['lighting']['emissiveEnabled'] = True
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)


def capture(exe, output, name):
    path, log = output/(name+'.ptbuf'), output/(name+'.log')
    command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
               '-WindowSize', '256', '144', '-EnablePathTracing', '-DebugPreviewResource', 'PathTracing.'+name,
               '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
    child = subprocess.run(command, cwd=output, timeout=180)
    if child.returncode:
        raise RuntimeError('Capture failed: '+name+' exit='+str(child.returncode))
    text = log.read_text(encoding='utf-8-sig')
    if '[ERROR]' in text or '[CORRUPTION]' in text:
        raise ValueError('D3D12 capture error: '+name)
    return read_buffer(path), dict(resource=name, command=command, sha256=sha(path), d3d12Errors=0)


def prepare(buffers, scene, output):
    meta = buffers['CurrentFrameRadiance'][0]
    common = {k: v for k, v in meta.items() if k not in ('resource', 'format')}
    for name, (other, _) in buffers.items():
        expected_format = 41 if name == 'ViewZ' else 34 if name == 'MotionVectors' else (
            10 if name in ('NormalRoughness', 'Albedo', 'MaterialF0Metallic') else 2)
        if (other['resource'] != 'PathTracing.'+name or
                other['format'] != expected_format or
                {k: v for k, v in other.items() if k not in ('resource', 'format')} != common):
            raise ValueError('Capture production mismatch: '+name)
    ray, continuation, surface = (buffers[name][1] for name in NAMES[:3])
    if (not (ray[..., 2] == 1).all() or not (surface[..., 1] == 1).all()
            or not (continuation[..., 3] > 0).all() or not (surface[..., 0] > 0).all()):
        raise ValueError('Closed fixture contains miss, untraced, or invalid continuation')
    if (meta['viewProjection'] != meta['previousViewProjection'] or
            np.abs(buffers['MotionVectors'][1]).max() > 2e-6):
        raise ValueError('Static camera transform/reprojection noise check failed')
    actual_distance = np.stack((surface[..., 0], ray[..., 2], ray[..., 0], ray[..., 3]), axis=-1)
    inputs = [buffers[name][1] for name in ('CurrentFrameRadiance', 'NormalRoughness', 'ViewZ', 'MotionVectors',
                                           'Albedo', 'MaterialF0Metallic', 'DenoiserPrimaryPosition')]+[actual_distance]
    inverse = np.asarray(meta['inverseViewProjection'], dtype=np.float32).T
    expected = reference(inputs, inverse)
    if (expected[8] != 0).any():
        raise ValueError('Converted pixels are not all numeric candidates')
    if inputs[0][..., :3].max() <= 0:
        raise ValueError('Fixture radiance is entirely black')
    output.mkdir()
    (output/'frame.bin').write_bytes(struct.pack('<16fII', *inverse.ravel(), meta['width'], meta['height']))
    for slot, values in enumerate(inputs):
        values.astype('<f4').tofile(output/f'in{slot}.bin')
    # Fixture uses origin, +Z forward, +Y up. Thus its LH view is identity.
    identity = np.eye(4)
    view_projection = np.linalg.inv(inverse.astype(float))
    camera = scene['camera']
    write_json(output/'native.json', dict(distanceSource='actual-continuation', nativeOptIn=True, staticCamera=True,
        samplesPerFrame=meta['samplesPerFrame'], primarySampling=meta['primarySampling'],
        frameId=meta['rrConversion']['frameId'], sampleStartIndex=meta['sampleStartIndex'],
        worldToView=identity.ravel().tolist(), viewToWorld=identity.ravel().tolist(),
        viewToClip=view_projection.ravel().tolist(), clipToView=inverse.ravel().tolist(),
        cameraPosition=camera['position'], cameraRight=[1, 0, 0], cameraUp=[0, 1, 0], cameraForward=[0, 0, 1],
        nearZ=camera['nearZ'], farZ=camera['farZ'], fovRadians=float(np.deg2rad(camera['verticalFovDegrees']))))
    return meta, expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--replay-exe', type=Path, required=True)
    parser.add_argument('--shader', type=Path, required=True)
    parser.add_argument('--reuse-captures', type=Path)
    args = parser.parse_args()
    output, source = args.output.resolve(), args.source.resolve(strict=True)
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    fixture(source, output)
    exe, replay = args.exe.resolve(strict=True), args.replay_exe.resolve(strict=True)
    report = dict(status='running', captures=[], failures=[], exeSha256=sha(exe), replaySha256=sha(replay),
                  producerShaderSha256=sha(exe.parent/'shaders_PathTracing_CSMain.cso'),
                  converterSha256=sha(args.shader), scriptSha256=sha(Path(__file__)))
    report['fixtureHashes'] = {name: sha(output/name) for name in ('scene.json', 'preset.json', 'box.gltf')}
    (output/'validation-script.py').write_bytes(Path(__file__).read_bytes())
    write_json(output/'report.json', report)
    try:
        if args.reuse_captures:
            for name in ('scene.json', 'preset.json', 'box.gltf'):
                if sha(args.reuse_captures/name) != report['fixtureHashes'][name]:
                    raise ValueError('Reused fixture mismatch: '+name)
        buffers = {}
        for name in NAMES:
            if args.reuse_captures:
                path = args.reuse_captures.resolve(strict=True)/(name+'.ptbuf')
                buffers[name], record = read_buffer(path), dict(resource=name, reused=str(path), sha256=sha(path))
            else:
                buffers[name], record = capture(exe, output, name)
            report['captures'].append(record)
            write_json(output/'report.json', report)
            print('Captured '+name, flush=True)
        meta, expected = prepare(buffers, json.loads((output/'scene.json').read_text()), output/'inputs')
        command = [str(replay), str(args.shader.resolve(strict=True)), str(output/'inputs'), str(output/'gpu'), '--hardware']
        child = subprocess.run(command, cwd=output, capture_output=True, text=True, timeout=180)
        (output/'native.log').write_text(child.stdout+child.stderr, encoding='utf-8')
        report['command'] = command
        if child.returncode:
            raise RuntimeError('Native replay failed: '+child.stdout+child.stderr)
        for slot, target in enumerate(expected):
            values = np.fromfile(output/'gpu'/f'out{slot}.bin', dtype='<u4' if slot == 8 else '<f4').reshape(target.shape)
            if not np.allclose(values, target, atol=5e-5, rtol=5e-5):
                raise ValueError('GPU conversion mismatch: '+str(slot))
        native = np.fromfile(output/'gpu'/'native-rgba32.bin', dtype='<f4').reshape(meta['height'], meta['width'], 4)
        if not np.isfinite(native[..., :3]).all():
            raise ValueError('Native output RGB is nonfinite')
        report['native'] = json.loads((output/'gpu'/'native-result.json').read_text())
        report['outputSha256'] = sha(output/'gpu'/'native-rgba32.bin')
        report['outputRgbRange'] = [float(native[..., :3].min()), float(native[..., :3].max())]
        if report['native'].get('nativeEvaluateCallCount') != 1:
            raise ValueError('Expected exactly one native RR evaluation')
        report['negativeChecks'] = []
        for case in ('native-off', 'invalid-distance'):
            folder = output/case
            shutil.copytree(output/'inputs', folder/'inputs')
            if case == 'native-off':
                metadata = json.loads((folder/'inputs'/'native.json').read_text())
                metadata['nativeOptIn'] = False
                write_json(folder/'inputs'/'native.json', metadata)
            else:
                path = folder/'inputs'/'in7.bin'
                distance = np.fromfile(path, dtype='<f4')
                distance[0] = -1
                distance.tofile(path)
            command = [str(replay), str(args.shader.resolve()), str(folder/'inputs'), str(folder/'gpu'), '--hardware']
            child = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=180)
            (folder/'native.log').write_text(child.stdout+child.stderr, encoding='utf-8')
            rejected = json.loads((folder/'gpu'/'admission-result.json').read_text())
            if (child.returncode == 0 or rejected['admitted'] or rejected['nativeEvaluateCallCount'] != 0 or
                    (folder/'gpu'/'native-rgba32.bin').exists()):
                raise ValueError('Native evaluate was not suppressed: '+case)
            report['negativeChecks'].append(dict(case=case, command=command, exitCode=child.returncode, **rejected))
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps({key: value for key, value in report.items() if key != 'captures'}), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
