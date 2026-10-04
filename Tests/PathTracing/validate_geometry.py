"""Validate native PT normals for glTF normal maps and transformed instances."""
import argparse
import base64
from datetime import datetime, timezone
from io import BytesIO
import json
from pathlib import Path
import subprocess

from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
NORMAL_BYTES = [180, 100, 240]


def unit(value):
    import numpy as np
    value = np.asarray(value, dtype=float)
    return value / np.linalg.norm(value)


def surface_frame(mirrored=False, angle=30):
    import numpy as np
    sign = -1 if mirrored else 1
    # glTF node: S then 90-degree Z rotation, followed by RH-to-LH conversion.
    node = np.diag([1, 1, -1]) @ np.array([[0, -1, 0], [1, 0, 0], [0, 0, 1]]) @ np.diag([2*sign, 1, .5])
    a = np.deg2rad(angle)
    instance = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]]) @ np.diag([1, .7, 2])
    transform = instance @ node
    normal = unit(np.linalg.inv(transform).T @ unit([1, 1, 2]))
    tangent = transform @ unit([1, -1, 0])
    tangent = unit(tangent - normal * np.dot(normal, tangent))
    bitangent = np.cross(normal, tangent) * np.sign(np.linalg.det(transform))
    return normal, tangent, bitangent


def expected_normal(mapped, mirrored=False, angle=30, normal_scale=1):
    import numpy as np
    normal, tangent, bitangent = surface_frame(mirrored, angle)
    if not mapped:
        return normal
    encoded = np.asarray(NORMAL_BYTES)/255*2-1
    encoded[:2] *= normal_scale
    encoded = unit(encoded)
    return unit(tangent*encoded[0] + bitangent*encoded[1] + normal*encoded[2])


def fixtures(output, name, mapped, mirrored, instances, separate_meshes=False):
    import numpy as np
    from PIL import Image
    gltf = json.loads((ROOT/'Tests/Fixtures/Gltf/surface-transforms.gltf').read_text())
    node = gltf['nodes'][1 if mirrored else 0]
    node['translation'] = [0, -4/3, 1/6]
    gltf['nodes'] = [node]
    gltf['scenes'][0]['nodes'] = [0]
    material = dict(pbrMetallicRoughness=dict(baseColorFactor=[.25, .5, .75, 1], metallicFactor=0, roughnessFactor=.37))
    if mapped:
        image = BytesIO()
        Image.new('RGB', (1, 1), tuple(NORMAL_BYTES)).save(image, format='PNG')
        gltf['images'] = [dict(uri='data:image/png;base64,'+base64.b64encode(image.getvalue()).decode())]
        gltf['textures'] = [dict(source=0)]
        material['normalTexture'] = dict(index=0)
    gltf['materials'] = [material]
    gltf['meshes'][0]['primitives'][0]['material'] = 0
    asset = output/(name+'.gltf')
    write_json(asset, gltf)
    scene = json.loads((ROOT/'Assets/Scenes/PathTracingValidation/input-plane/scene.json').read_text())
    scene.update(sceneId=name, name=name, assets=[dict(id='surface', type='gltf', path=asset.name)])
    scene['nodes'] = []
    for i, angle in enumerate([30, -15] if instances else [30]):
        radians = np.deg2rad(angle)/2
        scene['nodes'].append(dict(id=f'instance-{i}', name=f'instance-{i}', type='gltf', parentId=None,
            assetId='surface', translation=[-1.5 if i == 0 else 1.5, 0, 0] if instances else [0, 0, 0],
            rotation=[0, float(np.sin(radians)), 0, float(np.cos(radians))], scale=[1, .7, 2], visible=True))
    scene['camera'].update(position=[0, 0, -7 if instances else -5], target=[0, 0, 0])
    if separate_meshes:
        second = json.loads(json.dumps(gltf))
        second['nodes'][0]['scale'][0] *= -1
        second['materials'][0]['pbrMetallicRoughness']['roughnessFactor'] = .71
        second['materials'][0]['normalTexture']['scale'] = .6
        second_asset = output/(name+'-second.gltf')
        write_json(second_asset, second)
        scene['assets'].append(dict(id='surface-second', type='gltf', path=second_asset.name))
        scene['nodes'][1]['assetId'] = 'surface-second'
    preset = json.loads((ROOT/'Assets/Scenes/PathTracingValidation/input-plane/render-preset.json').read_text())
    scene_path, preset_path = output/(name+'-scene.json'), output/(name+'-preset.json')
    scene['renderPreset'] = preset_path.name
    write_json(scene_path, scene)
    write_json(preset_path, preset)
    return scene_path, preset_path, asset


def analyze(path, mapped, mirrored, instances, separate_meshes=False):
    import numpy as np
    meta, values = read_buffer(path)
    if meta['resource'] != 'PathTracing.NormalRoughness':
        raise ValueError('Wrong guide resource')
    hit = np.linalg.norm(values[:, :, :3], axis=2) > .5
    checks = []
    for i, angle in enumerate([30, -15] if instances else [30]):
        mask = hit.copy()
        if instances:
            if i == 0:
                mask[:, meta['width']//2:] = False
            else:
                mask[:, :meta['width']//2] = False
        samples = values[mask]
        if len(samples) < 1000:
            raise ValueError('Insufficient visible surface pixels')
        second = separate_meshes and i == 1
        expected = np.append(expected_normal(mapped, mirrored or second, angle, .6 if second else 1),
            .71 if second else .37)
        error = float(np.abs(samples-expected).max())
        checks.append(dict(instance=i, pixelCount=len(samples), expected=expected.tolist(),
            observedMean=samples.mean(axis=0).tolist(), maxAbsoluteError=error, tolerance=.001,
            passed=error <= .001))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/geometry')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'),
        sourceSha256={p: sha(ROOT/p) for p in ['GltfLoader.cpp', 'Shaders/SurfaceTransform.hlsli',
            'Shaders/SceneRayQuery.hlsli', 'Shaders/shaders_GBuffer.hlsl', 'Tests/PathTracing/validate_geometry.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        records=[], status='running')
    for name, mapped, mirrored, instances in [('normal', False, False, False), ('mapped', True, False, False),
            ('mirrored-node', True, True, False), ('instances', True, False, True),
            ('separate-meshes', True, False, True)]:
        separate_meshes = name == 'separate-meshes'
        scene, preset, asset = fixtures(output, name, mapped, mirrored, instances, separate_meshes)
        path, log = output/(name+'.ptbuf'), output/(name+'.log')
        command = [str(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene), '-RenderPreset', str(preset),
            '-EnablePathTracing', '-PathTracingSeed', '7', '-DebugPreviewResource', 'PathTracing.NormalRoughness',
            '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
        record = dict(name=name, command=command, sceneSha256=sha(scene), presetSha256=sha(preset), assetSha256=sha(asset))
        if separate_meshes:
            record['secondAssetSha256'] = sha(output/(name+'-second.gltf'))
        try:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            subprocess.run(command, cwd=ROOT, startupinfo=startup, check=True, timeout=180)
            errors = [s for s in log.read_text(encoding='utf-8-sig').splitlines() if '[ERROR]' in s or '[CORRUPTION]' in s]
            if errors:
                raise ValueError('D3D12 errors: '+str(errors[:2]))
            checks = analyze(path, mapped, mirrored, instances, separate_meshes)
            record.update(checks=checks, passed=all(c['passed'] for c in checks), d3d12Errors=0, sha256=sha(path))
        except Exception as error:
            record.update(passed=False, failure=str(error))
        report['records'].append(record)
        print(json.dumps(record), flush=True)
        write_json(output/'report.json', report)
    report['status'] = 'passed' if all(r['passed'] for r in report['records']) else 'failed'
    write_json(output/'report.json', report)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
