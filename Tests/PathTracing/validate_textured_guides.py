"""Compare native PT guides against known glTF texture bytes and tangent frames."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import numpy as np
from make_textured_fixture import COLORS, NORMALS, ROUGHNESS, BASE_FACTOR, ROUGHNESS_FACTOR
from validate_inputs import ROOT, read_buffer, plane_hits
from validate_part1 import sha, write_json


def expected_regions(normal_scale, mirrored):
    encoded = np.asarray(COLORS, dtype=float)[:, :3]/255
    color = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
    albedo = np.column_stack((color*np.asarray(BASE_FACTOR[:3]), np.ones(4)))
    tangent = np.asarray(NORMALS, dtype=float)[:, :3]/255*2-1
    tangent[:, :2] *= normal_scale
    normals = tangent * [-1 if mirrored else 1, 1, -1]
    normals /= np.linalg.norm(normals, axis=1)[:, None]
    guide = np.column_stack((normals, np.asarray(ROUGHNESS)/255*ROUGHNESS_FACTOR))
    return albedo, guide


def analyze(path, variant):
    meta, raw = read_buffer(path)
    resource = meta['resource'].split('.')[-1]
    if resource not in ('Albedo', 'NormalRoughness') or meta['format'] != 10:
        raise ValueError('Expected native RGBA16 primary guide')
    xs, ys = np.meshgrid(np.arange(16, meta['width']-16, 4), np.arange(16, meta['height']-16, 4))
    xs, ys = xs.ravel(), ys.ravel()
    world, _, _ = plane_hits(meta, xs, ys)
    mirrored = variant == 'mirrored'
    u = ((-world[:, 0] if mirrored else world[:, 0])+10)/20
    v = (world[:, 1]+10)/20
    # Exclude the bilinear transition band; these samples test constant texel regions.
    selected = ((abs(u-.5) > 1/16) & (abs(v-.5) > 1/16) &
                (u > 1/16) & (u < 15/16) & (v > 1/16) & (v < 15/16))
    region = (v[selected] >= .5).astype(int)*2+(u[selected] >= .5).astype(int)
    counts = np.bincount(region, minlength=4)
    if np.min(counts) < 1000:
        raise ValueError('Insufficient samples in one or more texture regions')
    expected_albedo, expected_normal = expected_regions(0 if variant == 'flat' else .5, mirrored)
    expected = (expected_albedo if resource == 'Albedo' else expected_normal)[region]
    observed = raw[ys[selected], xs[selected]]
    tolerance = .0015 if resource == 'Albedo' else .002
    error = abs(observed-expected)
    means = np.asarray([observed[region == i].mean(axis=0) for i in range(4)])
    distinct = np.linalg.norm(means[:, :3]-means[0, :3], axis=1).max()
    if (resource == 'Albedo' or variant != 'flat') and distinct < .05:
        raise ValueError('Spatial texture variation is missing')
    return meta, dict(resource=resource, variant=variant, pixelCount=int(sum(counts)),
        regionPixelCounts=counts.tolist(), expectedRegions=(expected_albedo if resource == 'Albedo' else expected_normal).tolist(),
        observedRegionMeans=means.tolist(), maxAbsoluteError=float(error.max()),
        componentMaxError=error.max(axis=0).tolist(), tolerance=tolerance,
        passed=bool((error <= tolerance).all()),
        excluded='Bilinear transitions and texture borders; 1/16 UV margin')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    args = parser.parse_args()
    args.exe = args.exe.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    fixture = ROOT/'Assets/Scenes/PathTracingValidation/input-textured'
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        executable=str(args.exe), executableSha256=sha(args.exe),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        sourceSha256={p: sha(ROOT/p) for p in ['GltfLoader.cpp', 'Shaders/SceneRayQuery.hlsli',
            'Shaders/shaders_PathTracing.hlsl', 'Tests/PathTracing/validate_textured_guides.py',
            'Tests/PathTracing/make_textured_fixture.py']}, records=[], failures=[], status='running')
    for variant in ('mapped', 'flat', 'mirrored'):
        scene = fixture/('scene.json' if variant == 'mapped' else 'scene-'+variant+'.json')
        for resource in ('NormalRoughness', 'Albedo'):
            name = variant+'-'+resource
            path, log = args.output/(name+'.ptbuf'), args.output/(name+'.log')
            command = [str(args.exe), '-SceneFile', str(scene), '-RenderPreset', str(fixture/'render-preset.json'),
                '-EnablePathTracing', '-PathTracingSeed', '7', '-DebugPreviewResource', 'PathTracing.'+resource,
                '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
            record = dict(name=name, command=command, sceneSha256=sha(scene),
                gltfSha256=sha(fixture/(variant+'.gltf')), presetSha256=sha(fixture/'render-preset.json'))
            report['records'].append(record)
            try:
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
                try:
                    code = child.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                    raise
                if code:
                    raise RuntimeError(f'App exit {code}')
                errors = [line for line in log.read_text(encoding='utf-8-sig').splitlines()
                          if '[ERROR]' in line or '[CORRUPTION]' in line]
                if errors:
                    raise RuntimeError(str(errors[:2]))
                meta, result = analyze(path, variant)
                record.update(metadata=meta, result=result, sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
                if not result['passed']:
                    raise RuntimeError('Textured guide numeric comparison failed')
            except Exception as error:
                record['failure'] = str(error)
                report['failures'].append(dict(name=name, error=str(error)))
            print(name, json.dumps(record.get('result', record.get('failure'))), flush=True)
            write_json(args.output/'report.json', report)
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(args.output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
