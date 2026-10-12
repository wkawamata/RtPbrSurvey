"""Audit opt-in dedicated lobe rays against planar geometry and sampling references."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer, hash_uint
from validate_part1 import sha, write_json
from validate_secondary_geometry import planes_from_scene, primary_rays, intersect_planes

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('Albedo', 'DenoiserDiffuseDirection', 'DenoiserSpecularDirection',
             'DenoiserDiffuseDistance', 'DenoiserSpecularDistance', 'DenoiserPrimaryPosition')


def normalize(values):
    return values / np.linalg.norm(values, axis=-1, keepdims=True)


def lobe_random(meta, xs, ys, salt):
    add = np.uint32(0x9e3779b9)
    seed = np.uint32((meta['randomSeed'] + int(add)) & 0xffffffff)
    base = hash_uint(np.asarray(xs, dtype=np.uint32) ^ hash_uint(
        np.asarray(ys, dtype=np.uint32) ^ hash_uint(meta['sampleStartIndex']) ^ hash_uint(seed)))
    state = hash_uint(base ^ np.uint32(salt))
    state = hash_uint(state + add)
    u = (state >> 8).astype(float) / 16777216
    state = hash_uint(state + add)
    v = (state >> 8).astype(float) / 16777216
    return u, v


def sample_lobes(meta, xs, ys, normal, view, roughness):
    helper = np.zeros_like(normal)
    helper[:, 1] = 1
    vertical = abs(normal[:, 1]) >= .999
    helper[vertical] = [1, 0, 0]
    tangent = normalize(np.cross(helper, normal))
    bitangent = np.cross(normal, tangent)
    local_view = np.column_stack((np.sum(view*tangent, axis=-1),
        np.sum(view*bitangent, axis=-1), np.sum(view*normal, axis=-1)))
    u, v = lobe_random(meta, xs, ys, 0xa511e9b3)
    diffuse = np.column_stack((np.sqrt(u)*np.cos(2*np.pi*v),
        np.sqrt(u)*np.sin(2*np.pi*v), np.sqrt(1-u)))
    diffuse_pdf = diffuse[:, 2]/np.pi
    u, v = lobe_random(meta, xs, ys, 0x63d83595)
    alpha = max(roughness*roughness, .001)
    stretched = normalize(local_view * [alpha, alpha, 1])
    length = np.linalg.norm(stretched[:, :2], axis=-1)
    t1 = np.tile([1., 0., 0.], (len(xs), 1))
    nonzero = length > 0
    t1[nonzero] = np.column_stack((-stretched[nonzero, 1], stretched[nonzero, 0],
        np.zeros(nonzero.sum())))/length[nonzero, None]
    t2 = np.cross(stretched, t1)
    x = np.sqrt(u)*np.cos(2*np.pi*v)
    blend = (1+stretched[:, 2])/2
    y = (1-blend)*np.sqrt(np.maximum(0, 1-x*x)) + blend*np.sqrt(u)*np.sin(2*np.pi*v)
    half = x[:, None]*t1 + y[:, None]*t2 + np.sqrt(np.maximum(0, 1-x*x-y*y))[:, None]*stretched
    half[:, 2] = np.maximum(0, half[:, 2])
    half = normalize(half*[alpha, alpha, 1])
    specular = 2*np.sum(local_view*half, axis=-1)[:, None]*half-local_view
    denominator = half[:, 0]**2 + half[:, 1]**2 + alpha*alpha*half[:, 2]**2
    distribution = alpha*alpha/(np.pi*denominator**2)
    specular_pdf = distribution/(2*(local_view[:, 2] +
        np.sqrt(alpha*alpha + (1-alpha*alpha)*local_view[:, 2]**2)))
    specular_pdf[specular[:, 2] <= 0] = 0
    to_world = lambda d: normalize(tangent*d[:, :1]+bitangent*d[:, 1:2]+normal*d[:, 2:3])
    return ((to_world(diffuse), diffuse_pdf), (to_world(specular), specular_pdf))


def assess(buffers, scene):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected dedicated distance resources')
    meta = buffers['Albedo'][0]
    if (meta.get('primarySampling') != 'pixel-center' or meta.get('samplesPerFrame') != 1 or
            meta.get('denoiserDistanceDiagnostics') is not True):
        raise ValueError('Requires enabled single-sample pixel-center cohort')
    for name, (other, values) in buffers.items():
        if other['resource'] != 'PathTracing.'+name or other['format'] != (10 if name == 'Albedo' else 2):
            raise ValueError('Resource identity/format mismatch')
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex', 'viewProjection',
                    'previousViewProjection', 'inverseViewProjection', 'primarySampling',
                    'samplesPerFrame', 'denoiserDistanceDiagnostics', 'rayTMin', 'rayTMax', 'normalBias'):
            if other.get(key) != meta[key]:
                raise ValueError('Cohort mismatch: '+key)
        if values.shape != (meta['height'], meta['width'], 4) or not np.isfinite(values).all():
            raise ValueError('Invalid array')
    materials = scene['materials']
    if not materials or any(m['metallic'] != .35 or m['roughness'] != .55 for m in materials):
        raise ValueError('Requires uniform .35 metallic / .55 roughness fixture')
    if any(not any(c > 0 for c in m['baseColor'][:3]) for m in materials):
        raise ValueError('Requires active lobes')
    ray_min, ray_max, bias = (float(meta[k]) for k in ('rayTMin', 'rayTMax', 'normalBias'))
    if not np.isfinite([ray_min, ray_max, bias]).all() or not 0 < ray_min < ray_max or bias < 0:
        raise ValueError('Invalid ray parameters')
    planes = planes_from_scene(scene)
    ys, xs = np.mgrid[8:meta['height']-8:4, 8:meta['width']-8:4]
    xs, ys = xs.ravel(), ys.ravel()
    origins, primary_dir = primary_rays(meta, xs, ys)
    primary_t, primary_index, edge = intersect_planes(origins, primary_dir, planes, ray_min, ray_max)
    primary_hit = primary_index >= 0
    selected = primary_hit & ~edge
    if selected.sum() < 100:
        raise ValueError('Insufficient primary coverage')
    checks = dict(primaryValidity=bool(np.array_equal(
        buffers['Albedo'][1][ys[~edge], xs[~edge], 3] == 1, primary_hit[~edge])))
    position_data = buffers['DenoiserPrimaryPosition'][1][ys, xs]
    checks['positionValidity'] = bool(np.array_equal(position_data[~edge, 3] == 1, primary_hit[~edge]))
    checks['positionMissZero'] = bool((position_data[~primary_hit & ~edge] == 0).all())
    for lobe in ('Diffuse', 'Specular'):
        data = buffers['Denoiser'+lobe+'Distance'][1]
        checks[lobe+'States'] = bool(np.isin(data[..., 1], [0, 1, 2, 3, 4]).all())
        checks[lobe+'NonhitZero'] = bool((data[..., 0][data[..., 1] != 1] == 0).all())
        checks[lobe+'ReservedZero'] = bool((data[..., 3] == 0).all())
        checks[lobe+'PrimaryMiss'] = bool((data[ys[~primary_hit & ~edge], xs[~primary_hit & ~edge]] == 0).all())
    xs, ys = xs[selected], ys[selected]
    expected_point = origins[selected] + primary_dir[selected]*primary_t[selected, None]
    point = position_data[selected, :3]
    position_bound = 1e-4+1e-5*np.maximum(1, abs(expected_point))
    checks['primaryPosition'] = bool((abs(point-expected_point) <= position_bound).all())
    normal = np.asarray([planes[i][3] for i in primary_index[selected]])
    sampled = sample_lobes(meta, xs, ys, normal, -primary_dir[selected], .55)
    # Reproduce only the recorded-point bias addition, not the intersection.
    biased_origin = (point.astype(np.float32)+normal.astype(np.float32)*np.float32(bias)).astype(float)
    origin_offset = biased_origin-point
    records = {}
    for lobe, (expected_direction, expected_pdf) in zip(('Diffuse', 'Specular'), sampled):
        direction = buffers['Denoiser'+lobe+'Direction'][1][ys, xs]
        data = buffers['Denoiser'+lobe+'Distance'][1][ys, xs]
        checks[lobe+'Direction'] = bool((abs(direction[:, :3]-expected_direction) <=
            .0002+.002*abs(expected_direction)).all())
        checks[lobe+'Pdf'] = bool((abs(direction[:, 3]-expected_pdf) <= .0002+.002*abs(expected_pdf)).all())
        checks[lobe+'Unit'] = bool((abs(np.linalg.norm(direction[:, :3], axis=-1)-1) < 2e-5).all())
        valid = (expected_pdf > 0) & (np.sum(expected_direction*normal, axis=-1) > 0)
        t, index, boundary = intersect_planes(biased_origin, direction[:, :3], planes, ray_min, ray_max)
        mask = ~boundary & (abs(np.sum(expected_direction*normal, axis=-1)) > 1e-5)
        expected_state = np.where(valid, np.where(index >= 0, 1, 2), 3)
        checks[lobe+'State'] = bool(np.array_equal(data[mask, 1], expected_state[mask]))
        hits = mask & valid & (index >= 0)
        misses = mask & valid & (index < 0)
        invalid = mask & ~valid
        bound = 1e-4+5e-5*t[hits]
        corrected = np.linalg.norm(origin_offset[hits]+direction[hits, :3]*t[hits, None], axis=-1)
        checks[lobe+'HitT'] = bool((abs(data[hits, 2]-t[hits]) <= bound).all())
        checks[lobe+'Distance'] = bool((abs(data[hits, 0]-corrected) <= bound).all())
        checks[lobe+'MissTMax'] = bool((data[misses, 2] == ray_max).all())
        checks[lobe+'InvalidTZero'] = bool((data[invalid, 2] == 0).all())
        checks[lobe+'Coverage'] = bool(hits.sum() >= 100 and misses.sum() >= 100)
        records[lobe] = dict(hits=int(hits.sum()), misses=int(misses.sum()), invalid=int(invalid.sum()),
            boundaryExcluded=int((~mask).sum()),
            distanceMaxError=float(abs(data[hits, 0]-corrected).max()) if hits.any() else None,
            directionMaxError=float(abs(direction[:, :3]-expected_direction).max()))
    return dict(passed=all(checks.values()), checks=checks, lobes=records,
        limitation='Uniform active-lobe finite planes, fixed ROI/edge masks, first primary sample; not NRD packing.')


def assess_black_metal(buffers, scene):
    resources = ('Albedo', 'DenoiserDiffuseDistance', 'DenoiserSpecularDistance')
    if set(buffers) != set(resources):
        raise ValueError('Missing black-metal resources')
    if not scene['materials'] or any(m['metallic'] != 1 or m['baseColor'] != [0, 0, 0, 1]
                                    for m in scene['materials']):
        raise ValueError('Requires opaque black-metal fixture')
    meta = buffers['Albedo'][0]
    if (meta.get('denoiserDistanceDiagnostics') is not True or
            meta.get('primarySampling') != 'pixel-center' or meta.get('samplesPerFrame') != 1):
        raise ValueError('Invalid black-metal sampling cohort')
    keys = ('width', 'height', 'randomSeed', 'sampleStartIndex', 'viewProjection',
            'previousViewProjection', 'inverseViewProjection', 'primarySampling',
            'samplesPerFrame', 'denoiserDistanceDiagnostics', 'rayTMin', 'rayTMax', 'normalBias')
    for name, (other, values) in buffers.items():
        if (other['resource'] != 'PathTracing.'+name or other['format'] != (10 if name == 'Albedo' else 2)
                or any(other.get(k) != meta[k] for k in keys) or
                values.shape != (meta['height'], meta['width'], 4) or not np.isfinite(values).all()):
            raise ValueError('Black-metal cohort mismatch')
    primary = buffers['Albedo'][1][..., 3] == 1
    diffuse = buffers['DenoiserDiffuseDistance'][1]
    specular = buffers['DenoiserSpecularDistance'][1]
    ys, xs = np.mgrid[8:meta['height']-8:4, 8:meta['width']-8:4]
    ys, xs = ys.ravel(), xs.ravel()
    origins, directions = primary_rays(meta, xs, ys)
    _, indices, edges = intersect_planes(origins, directions, planes_from_scene(scene), meta['rayTMin'], meta['rayTMax'])
    checks = dict(primaryValidity=bool(np.array_equal(primary[ys[~edges], xs[~edges]], indices[~edges] >= 0)),
        primaryCoverage=bool(primary.sum() >= 100),
        blackAlbedo=bool((buffers['Albedo'][1][primary, :3] == 0).all()),
        diffuseInactive=bool((diffuse[primary] == [0, 4, 0, 0]).all()),
        specularNotInactive=bool(np.isin(specular[primary, 1], [1, 2, 3]).all()),
        specularNonhitZero=bool((specular[..., 0][specular[..., 1] != 1] == 0).all()),
        specularReservedZero=bool((specular[..., 3] == 0).all()),
        primaryMissZero=bool((diffuse[~primary] == 0).all() and (specular[~primary] == 0).all()))
    return dict(passed=all(checks.values()), checks=checks, primaryPixels=int(primary.sum()),
        specularHits=int((specular[..., 1] == 1).sum()),
        specularMisses=int((specular[..., 1] == 2).sum()),
        specularInvalid=int((specular[..., 1] == 3).sum()),
        limitation='Black-metal lobe-activity regression only; not a distance or NRD quality oracle.')


def capture(exe, folder, resource, preset, label):
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    command = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(preset),
        '-EnablePathTracing', '-DebugPreviewResource', 'PathTracing.'+resource,
        '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        if child.wait(timeout=180) != 0:
            raise RuntimeError('Capture failed: '+label)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    text = log.read_text(encoding='utf-8-sig')
    if '[ERROR]' in text or '[CORRUPTION]' in text:
        raise RuntimeError('D3D12 error: '+label)
    return read_buffer(path), dict(label=label, resource=resource, sha256=sha(path), command=command, d3d12Errors=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--black-metal', action='store_true')
    parser.add_argument('--normal-guide', action='store_true')
    args = parser.parse_args()
    output, exe = args.output.resolve(), args.exe.resolve(strict=True)
    if output.exists() and any(output.iterdir()):
        parser.error('Output directory must be empty')
    source = ROOT/'Assets/Scenes/PathTracingValidation/two-surface-indirect'
    scene = json.loads((source/'scene.json').read_text())
    for material in scene['materials']:
        material.update(metallic=.35, roughness=.55)
        if args.black_metal:
            material.update(metallic=1, baseColor=[0, 0, 0, 1])
    scene['renderPreset'] = 'on.json'
    preset = json.loads((source/'render-preset.json').read_text())
    preset['pathTracing'].update(accumulate=False, samplesPerFrame=1, pixelCenterPrimary=True,
        maxBounces=2, randomSeed=7, debugOutput=3, russianRouletteEnabled=False,
        directLightingEnabled=False, emissiveEnabled=False, environmentEnabled=True)
    preset['shadow'].update(rayTMin=.001, rayTMax=100., normalBias=.001)
    preset['pathTracing']['denoiserDistanceDiagnostics'] = True
    write_json(output/'scene.json', scene)
    write_json(output/'on.json', preset)
    preset['pathTracing']['denoiserDistanceDiagnostics'] = False
    write_json(output/'off.json', preset)
    report = dict(schemaVersion=1, status='running', records=[], failures=[],
        exeSha256=sha(exe), shaderSha256=sha(exe.parent/'shaders_PathTracing_CSMain.cso'),
        scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'))
    try:
        buffers = {}
        resources = ('Albedo', 'DenoiserDiffuseDistance', 'DenoiserSpecularDistance') if args.black_metal else RESOURCES
        for resource in resources:
            buffers[resource], record = capture(exe, output, resource, output/'on.json', resource)
            report['records'].append(record)
            write_json(output/'report.json', report)
            print('Captured '+resource, flush=True)
        if args.normal_guide:
            for guide in ('NormalRoughness', 'ViewZ'):
                _, record = capture(exe, output, guide, output/'on.json', guide)
                report['records'].append(record)
                write_json(output/'report.json', report)
        on, record = capture(exe, output, 'CurrentFrameRadiance', output/'on.json', 'RadianceOn')
        report['records'].append(record)
        off, record = capture(exe, output, 'CurrentFrameRadiance', output/'off.json', 'RadianceOff')
        report['records'].append(record)
        if on[0]['denoiserDistanceDiagnostics'] is not True or off[0]['denoiserDistanceDiagnostics'] is not False:
            raise ValueError('Diagnostic flags not applied')
        on_meta, off_meta = dict(on[0]), dict(off[0])
        del on_meta['denoiserDistanceDiagnostics'], off_meta['denoiserDistanceDiagnostics']
        if on_meta != off_meta or not np.array_equal(on[1], off[1]):
            raise ValueError('Native radiance changed between diagnostic ON/OFF')
        report['nativeRadianceBitIdentical'] = True
        if args.black_metal:
            primary = buffers['Albedo'][1][..., 3] == 1
            nonzero = int((np.any(on[1][..., :3] > 0, axis=-1) & primary).sum())
            if nonzero == 0:
                raise ValueError('Black-metal fixture did not exercise nonzero primary radiance')
            report['nonzeroPrimaryRadiancePixels'] = nonzero
        report['assessment'] = assess_black_metal(buffers, scene) if args.black_metal else assess(buffers, scene)
        if not report['assessment']['passed']:
            raise ValueError('Dedicated distance assessment failed')
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
