"""Compare native raster GBuffer and PT inputs against each other and an independent oracle."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

import numpy as np
from compare_hdr import read_pfm
from validate_inputs import read_buffer
from validate_materials import make_fixture, expected_material
from validate_geometry import ROOT
from validate_part1 import sha, write_json


def interior(mask):
    result = np.logical_and.reduce([np.roll(np.roll(mask, y, axis=0), x, axis=1)
        for y in range(-2, 3) for x in range(-2, 3)])
    result[:2] = result[-2:] = False
    result[:, :2] = result[:, -2:] = False
    return result


def compare(pt, gb):
    if pt['NormalRoughness'].shape[:2] != gb['Normal'].shape[:2]:
        raise ValueError('Mismatched render dimensions')
    hit = interior((np.linalg.norm(pt['NormalRoughness'][:, :, :3], axis=2) > .5) &
        (gb['PBRParams'][:, :, 3] > .5))
    checks = []
    for index in range(2):
        mask = hit.copy()
        if index == 0:
            mask[:, mask.shape[1]//2:] = False
        else:
            mask[:, :mask.shape[1]//2] = False
        if mask.sum() < 1000:
            raise ValueError('Insufficient common interior pixels')
        for resource, a, b, expected, tolerance in [
            ('Normal', pt['NormalRoughness'][:, :, :3], gb['Normal'][:, :, :3],
                expected_material(index, 'NormalRoughness')[:3], .001),
            ('Roughness', pt['NormalRoughness'][:, :, 3:4], gb['PBRParams'][:, :, 1:2],
                np.array([.37]), .0025),
            ('Albedo', pt['Albedo'][:, :, :3], gb['Albedo'][:, :, :3],
                expected_material(index, 'Albedo')[:3], .0025),
            ('Emission', pt['Emissive'], gb['Emissive'][:, :, :3],
                expected_material(index, 'Emissive'), .001)]:
            av, bv = a[mask], b[mask]
            errors = dict(ptOracle=float(np.abs(av-expected).max()),
                gbufferOracle=float(np.abs(bv-expected).max()), pair=float(np.abs(av-bv).max()))
            checks.append(dict(material=index, resource=resource, pixelCount=int(mask.sum()),
                expected=expected.tolist(), ptMean=av.mean(axis=0, dtype=np.float64).tolist(),
                gbufferMean=bv.mean(axis=0, dtype=np.float64).tolist(), maxAbsoluteErrors=errors,
                tolerance=tolerance, passed=max(errors.values()) <= tolerance))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/gbuffer-pt')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scene, preset, asset = make_fixture(output)
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        workingTreeModified=True, executableSha256=sha(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'),
        sourceSha256={p: sha(ROOT/p) for p in ['Engine/RtPbrSurveyEngine.cpp', 'Renderer/ScreenshotCapture.cpp',
            'Shaders/shaders_GBuffer.hlsl', 'Shaders/SceneRayQuery.hlsli', 'Shaders/Material.hlsli',
            'Tests/PathTracing/validate_gbuffer_pt.py', 'Tests/PathTracing/validate_materials.py']},
        sceneSha256=sha(scene), assetSha256=sha(asset),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip(), records=[], status='running')
    pt, gb = {}, {}
    try:
        for backend, resources in [('pt', ['NormalRoughness', 'Albedo', 'Emissive']),
                ('gbuffer', ['Normal', 'Albedo', 'PBRParams', 'Emissive'])]:
            for resource in resources:
                data = json.loads(preset.read_text())
                data['renderingPath'] = 2 if backend == 'pt' else 1
                data['pathTracing']['accumulate'] = False
                data['pathTracing']['debugOutput'] = 2 if resource == 'Emissive' else 3
                write_json(preset, data)
                pfm = backend == 'pt' and resource == 'Emissive'
                path = output/(backend+'-'+resource+('.pfm' if pfm else '.ptbuf'))
                log = path.with_suffix('.log')
                name = ('PathTracing.' if backend == 'pt' else 'GBuffer.')+resource
                command = [str(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene),
                    '-RenderPreset', str(preset), '-CapturePath', str(path), '-CaptureAfterFrames', '30',
                    '-LogToFile', str(log), '-ExitAfterCapture']
                if backend == 'pt':
                    command += ['-EnablePathTracing', '-PathTracingSeed', '7']
                if not pfm:
                    command += ['-DebugPreviewResource', name]
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                subprocess.run(command, cwd=ROOT, startupinfo=startup, timeout=180, check=True)
                errors = [line for line in log.read_text(encoding='utf-8-sig').splitlines()
                    if '[ERROR]' in line or '[CORRUPTION]' in line]
                if errors:
                    raise ValueError('D3D12 errors: '+str(errors[:2]))
                if pfm:
                    height, width = pt['NormalRoughness'].shape[:2]
                    dimensions, values = read_pfm(path, [0, 0, width, height])
                    if dimensions != (width, height):
                        raise ValueError('Mismatched PFM dimensions')
                    values = np.asarray(values).reshape(height, width, 3)
                else:
                    meta, values = read_buffer(path)
                    if meta['resource'] != name:
                        raise ValueError('Wrong native resource')
                if not np.isfinite(values).all():
                    raise ValueError('Nonfinite input values')
                (pt if backend == 'pt' else gb)[resource] = values
                record = dict(backend=backend, resource=resource, command=command,
                    presetSha256=sha(preset), sha256=sha(path), d3d12Errors=0)
                report['records'].append(record)
                print(json.dumps(record), flush=True)
                write_json(output/'report.json', report)
        report['checks'] = compare(pt, gb)
        report['status'] = 'passed' if all(c['passed'] for c in report['checks']) else 'failed'
    except Exception as error:
        report.update(status='failed', failure=str(error))
    write_json(output/'report.json', report)
    print(json.dumps({k: v for k, v in report.items() if k in ['checks', 'status', 'failure']}), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
