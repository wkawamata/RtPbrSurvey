"""Validate imported material factors, shared textures, and factor-only emission."""
import argparse
import copy
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

from compare_hdr import read_pfm
from validate_geometry import ROOT, NORMAL_BYTES, fixtures, surface_frame, unit
from validate_inputs import read_buffer
from validate_part1 import sha, write_json

FACTORS = [[.2, .4, .6, .8], [.8, .1, .3, .4]]
EMISSION = [[.1, .3, .9], [.7, .2, .1]]
SCALES = [.35, 1.7]


def expected_material(index, resource, factor_only=False):
    import numpy as np
    rgb = np.asarray(NORMAL_BYTES)/255
    linear = np.where(rgb <= .04045, rgb/12.92, ((rgb+.055)/1.055)**2.4)
    if resource == 'Albedo':
        return np.append((1 if factor_only else linear)*np.asarray(FACTORS[index][:3]), 1)
    if resource == 'Emissive':
        return np.asarray(EMISSION[index]) * (1 if factor_only else linear)
    n, t, b = surface_frame()
    normal = rgb*2-1
    normal[:2] *= SCALES[index]
    return np.append(unit(t*normal[0]+b*normal[1]+n*normal[2]), .37)


def make_fixture(output):
    scene_path, preset_path, asset = fixtures(output, 'materials', True, False, False)
    scene = json.loads(scene_path.read_text())
    scene['camera']['position'] = [0, 0, -7]
    gltf = json.loads(asset.read_text())
    gltf['nodes'].append(copy.deepcopy(gltf['nodes'][0]))
    gltf['nodes'][0]['translation'][0] = -1.5
    gltf['nodes'][1]['translation'][0] = 1.5
    gltf['nodes'][1]['mesh'] = 1
    gltf['scenes'][0]['nodes'] = [0, 1]
    gltf['meshes'].append(copy.deepcopy(gltf['meshes'][0]))
    gltf['meshes'][1]['primitives'][0]['material'] = 1
    gltf['materials'] = [dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),
        baseColorFactor=FACTORS[i], metallicFactor=0, roughnessFactor=.37),
        emissiveTexture=dict(index=0), emissiveFactor=EMISSION[i],
        normalTexture=dict(index=0, scale=SCALES[i])) for i in range(2)]
    write_json(asset, gltf)
    write_json(scene_path, scene)
    return scene_path, preset_path, asset


def analyze(values, hit, resource, factor_only=False):
    import numpy as np
    checks = []
    for i in range(2):
        mask = hit.copy()
        if i == 0:
            mask[:, mask.shape[1]//2:] = False
        else:
            mask[:, :mask.shape[1]//2] = False
        observed = values[mask]
        if len(observed) < 1000:
            raise ValueError('Insufficient visible pixels for material '+str(i))
        expected = expected_material(i, resource, factor_only)
        error = float(np.abs(observed-expected).max())
        tolerance = .0015 if resource == 'Albedo' else .001
        checks.append(dict(material=i, pixelCount=len(observed), expected=expected.tolist(),
            observedMean=observed.mean(axis=0, dtype=np.float64).tolist(), maxAbsoluteError=error,
            tolerance=tolerance, passed=error <= tolerance))
    return checks


def main():
    import numpy as np
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/materials')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scene, preset, asset = make_fixture(output)
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        workingTreeModified=bool(subprocess.check_output(['git', 'diff', '--name-only'], cwd=ROOT, text=True).strip()),
        executableSha256=sha(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'),
        sourceSha256={p: sha(ROOT/p) for p in ['GltfLoader.cpp', 'GltfLoader.h', 'Scene/SceneBuilder.cpp',
            'Scene/Scene.h', 'Renderer/Material.h', 'Engine/RtPbrSurveyEngine.cpp', 'Shaders/Material.hlsli',
            'Shaders/SceneRayQuery.hlsli', 'Shaders/shaders_GBuffer.hlsl', 'Tests/PathTracing/validate_materials.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        records=[], status='running')
    hit = None
    for resource, factor_only in [('NormalRoughness', False), ('Albedo', False), ('Emissive', False),
            ('Emissive', True), ('Albedo', True)]:
        name = resource + ('-factor-only' if factor_only else '')
        if factor_only:
            gltf = json.loads(asset.read_text())
            for material in gltf['materials']:
                if resource == 'Emissive':
                    del material['emissiveTexture']
                else:
                    del material['pbrMetallicRoughness']['baseColorTexture']
            write_json(asset, gltf)
        preset_data = json.loads(preset.read_text())
        preset_data['pathTracing']['accumulate'] = False
        preset_data['pathTracing']['debugOutput'] = 2 if resource == 'Emissive' else 3
        write_json(preset, preset_data)
        path = output/(name+('.pfm' if resource == 'Emissive' else '.ptbuf'))
        log = output/(name+'.log')
        command = [str(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene), '-RenderPreset', str(preset),
            '-EnablePathTracing', '-PathTracingSeed', '7', '-CapturePath', str(path),
            '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
        if resource != 'Emissive':
            command += ['-DebugPreviewResource', 'PathTracing.'+resource]
        record = dict(name=name, command=command, sceneSha256=sha(scene), presetSha256=sha(preset), assetSha256=sha(asset))
        try:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            subprocess.run(command, cwd=ROOT, startupinfo=startup, timeout=180, check=True)
            errors = [s for s in log.read_text(encoding='utf-8-sig').splitlines() if '[ERROR]' in s or '[CORRUPTION]' in s]
            if errors:
                raise ValueError('D3D12 errors: '+str(errors[:2]))
            if resource == 'Emissive':
                if hit is None:
                    raise ValueError('Normal capture required for emission mask')
                height, width = hit.shape
                dimensions, pixels = read_pfm(path, [0, 0, width, height])
                if dimensions != (width, height):
                    raise ValueError('Mismatched emission dimensions')
                values = np.asarray(pixels).reshape(height, width, 3)
            else:
                meta, values = read_buffer(path)
                if meta['resource'] != 'PathTracing.'+resource:
                    raise ValueError('Wrong guide resource')
                if resource == 'NormalRoughness':
                    hit = np.linalg.norm(values[:, :, :3], axis=2) > .5
                    hit = np.logical_and.reduce([np.roll(np.roll(hit, y, axis=0), x, axis=1)
                        for y in range(-2, 3) for x in range(-2, 3)])
            checks = analyze(values, hit, resource, factor_only)
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
