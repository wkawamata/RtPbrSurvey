"""Independently intersect finite oriented planes to audit first secondary hitT."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer, sample_positions
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('SecondaryRayData', 'PrimaryContinuation', 'Albedo', 'SecondarySurfaceDistance')
EDGE_MARGIN = .002


def planes_from_scene(scene):
    if scene.get('assets'):
        raise ValueError('Only explicit primitive-plane fixtures are supported')
    planes = []
    for node in scene['nodes']:
        if not node.get('visible', True):
            continue
        primitive = node.get('primitive', {})
        if node.get('parentId') is not None or node['type'] != 'primitive' or primitive.get('kind') != 'plane':
            raise ValueError('Unsupported fixture geometry')
        scale = np.asarray(node['scale'], dtype=float)
        position = np.asarray(node['translation'], dtype=float)
        quaternion = np.asarray(node['rotation'], dtype=float)
        if (scale.shape != (3,) or position.shape != (3,) or quaternion.shape != (4,)
                or not np.isfinite(np.concatenate((scale, position, quaternion))).all()
                or (scale <= 0).any() or abs(np.linalg.norm(quaternion)-1) > 1e-5):
            raise ValueError('Invalid plane transform')
        x, y, z, w = quaternion / np.linalg.norm(quaternion)
        rotation = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                             [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                             [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        half_width = float(primitive['width'])*scale[0]/2
        half_depth = float(primitive['depth'])*scale[2]/2
        if not np.isfinite([half_width, half_depth]).all() or min(half_width, half_depth) <= 0:
            raise ValueError('Invalid plane extent')
        planes.append((position, rotation[:, 0], rotation[:, 2], rotation[:, 1], half_width, half_depth))
    if not planes:
        raise ValueError('No supported planes')
    return planes


def intersect_planes(origins, directions, planes, ray_min, ray_max):
    """Front-facing finite planes; distance is measured from the supplied origin."""
    count = len(origins)
    nearest = np.full(count, np.inf)
    indices = np.full(count, -1, dtype=int)
    boundary = np.zeros(count, dtype=bool)
    for index, (center, axis_x, axis_z, normal, half_width, half_depth) in enumerate(planes):
        denominator = directions @ normal
        facing = denominator < -1e-10
        distance = np.divide((center-origins) @ normal, denominator,
            out=np.full(count, np.inf), where=facing)
        candidate = facing & (distance >= ray_min) & (distance <= ray_max)
        points = origins + directions*np.where(candidate, distance, 0)[:, None]
        local_x = (points-center) @ axis_x
        local_z = (points-center) @ axis_z
        boundary |= candidate & (((abs(abs(local_x)-half_width) < EDGE_MARGIN) &
                    (abs(local_z) < half_depth+EDGE_MARGIN)) |
                    ((abs(abs(local_z)-half_depth) < EDGE_MARGIN) &
                    (abs(local_x) < half_width+EDGE_MARGIN)))
        valid = candidate & (abs(local_x) <= half_width) & (abs(local_z) <= half_depth)
        chosen = valid & (distance < nearest)
        nearest[chosen], indices[chosen] = distance[chosen], index
    return nearest, indices, boundary


def primary_rays(meta, xs, ys):
    u, v = sample_positions(meta, xs, ys)
    ndc = np.column_stack((2*(xs+u)/meta['width']-1, 1-2*(ys+v)/meta['height']))
    inverse = np.asarray(meta['inverseViewProjection'], dtype=float).T
    near = np.column_stack((ndc, np.zeros(len(xs)), np.ones(len(xs)))) @ inverse
    far = np.column_stack((ndc, np.ones(len(xs)), np.ones(len(xs)))) @ inverse
    origins = near[:, :3]/near[:, 3, None]
    directions = far[:, :3]/far[:, 3, None]-origins
    directions /= np.linalg.norm(directions, axis=-1, keepdims=True)
    return origins, directions


def assess(buffers, scene):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected geometry resources')
    meta = buffers['SecondaryRayData'][0]
    if meta.get('primarySampling') != 'pixel-center' or meta.get('samplesPerFrame') != 1:
        raise ValueError('Geometry cohort requires one pixel-center primary sample')
    ray_min, ray_max, bias = (float(meta[key]) for key in ('rayTMin', 'rayTMax', 'normalBias'))
    if not np.isfinite([ray_min, ray_max, bias]).all() or not 0 < ray_min < ray_max or bias < 0:
        raise ValueError('Invalid recorded ray parameters')
    for name in RESOURCES:
        other, values = buffers[name]
        if other['resource'] != 'PathTracing.'+name or other['format'] != (10 if name == 'Albedo' else 2):
            raise ValueError('Unexpected geometry resource/format')
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex', 'viewProjection',
                    'previousViewProjection', 'inverseViewProjection', 'primarySampling', 'samplesPerFrame',
                    'rayTMin', 'rayTMax', 'normalBias'):
            if other.get(key) != meta[key]:
                raise ValueError('Geometry cohort mismatch: '+key)
        if values.shape != (meta['height'], meta['width'], 4) or not np.isfinite(values).all():
            raise ValueError('Invalid geometry values')
    ys, xs = np.mgrid[8:meta['height']-8:4, 8:meta['width']-8:4]
    xs, ys = xs.ravel(), ys.ravel()
    secondary = buffers['SecondaryRayData'][1][ys, xs]
    surface_data = buffers['SecondarySurfaceDistance'][1][ys, xs]
    continuation = buffers['PrimaryContinuation'][1][ys, xs]
    validity = buffers['Albedo'][1][ys, xs, 3]
    planes = planes_from_scene(scene)
    origins, directions = primary_rays(meta, xs, ys)
    primary_distance, primary_index, primary_edge = intersect_planes(origins, directions, planes, ray_min, ray_max)
    primary_hit = primary_index >= 0
    traced = secondary[:, 2] != 0
    eligible = primary_hit & traced & ~primary_edge
    if eligible.sum() < 100:
        raise ValueError('Insufficient independently reconstructed primary hits')
    selected_planes = np.asarray([planes[index][3] for index in primary_index[eligible]])
    secondary_origin = origins[eligible] + directions[eligible]*primary_distance[eligible, None] + bias*selected_planes
    secondary_direction = continuation[eligible, :3]
    distance, index, secondary_edge = intersect_planes(secondary_origin, secondary_direction, planes, ray_min, ray_max)
    observed = secondary[eligible]
    selected = ~secondary_edge
    expected_hit = index[selected] >= 0
    expected = np.where(expected_hit, distance[selected], ray_max)
    actual = observed[selected, 0]
    error = abs(actual-expected)
    bound = 1e-4+5e-5*abs(expected)
    hit_directions = secondary_direction[selected][expected_hit]
    hit_normals = selected_planes[selected][expected_hit]
    physical_expected = np.linalg.norm(bias*hit_normals + expected[expected_hit, None]*hit_directions, axis=-1)
    physical_observed = np.linalg.norm(bias*hit_normals + actual[expected_hit, None]*hit_directions, axis=-1)
    physical_error = abs(physical_observed-physical_expected)
    checks = dict(primaryValidity=bool(np.isin(validity, [0, 1]).all() and
        np.array_equal(validity[~primary_edge], primary_hit[~primary_edge].astype(float))),
        primaryMissUntraced=bool((secondary[~primary_hit & ~primary_edge] == 0).all()),
        untracedZero=bool((secondary[~traced] == 0).all()),
        reservedZero=bool((secondary[:, 3] == 0).all()),
        surfaceReservedZero=bool((surface_data[:, 2:] == 0).all()),
        surfaceStatus=bool(np.array_equal(surface_data[:, 1], secondary[:, 2])),
        surfaceNoHitZero=bool((surface_data[secondary[:, 2] != 1, 0] == 0).all()),
        directionUnit=bool((abs(np.linalg.norm(secondary_direction, axis=-1)-1) < 1e-5).all()),
        continuationPdf=bool((continuation[eligible, 3] > 0).all()),
        proposals=bool(np.isin(observed[:, 1], [1, 2]).all()),
        secondaryStatus=bool(np.array_equal(observed[selected, 2], np.where(expected_hit, 1, 2))),
        secondaryDistance=bool((error <= bound).all()),
        reconstructedSurfaceDistance=bool((physical_error <= bound[expected_hit]).all()),
        publishedSurfaceDistance=bool((abs(surface_data[eligible][selected][expected_hit, 0] -
            physical_expected) <= bound[expected_hit]).all()),
        missSentinel=bool((actual[~expected_hit] == ray_max).all()),
        hitCoverage=bool(expected_hit.sum() >= 100), missCoverage=bool((~expected_hit).sum() >= 100))
    if not selected.any():
        raise ValueError('No unambiguous secondary samples')
    return dict(passed=all(checks.values()), checks=checks,
        hitPixels=int(expected_hit.sum()), missPixels=int((~expected_hit).sum()),
        primaryPixels=int(eligible.sum()), boundaryExcluded=int(primary_edge.sum()+secondary_edge.sum()),
        hitDistanceMaxError=float(error[expected_hit].max()) if expected_hit.any() else None,
        surfaceDistanceMaxError=float(physical_error.max()) if expected_hit.any() else None,
        publishedSurfaceDistanceMaxError=float(abs(surface_data[eligible][selected][expected_hit, 0] -
            physical_expected).max()) if expected_hit.any() else None,
        biasCorrectionMax=float(abs(physical_expected-expected[expected_hit]).max()) if expected_hit.any() else None,
        maxBoundNormalizedError=float((error/bound).max()),
        tolerance='1e-4 + 5e-5 * expected distance; fixed before GPU assessment',
        limitation='Rigid positive-scale finite planes, back-face culling, first secondary segment only; not NRD packing.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    output, exe = args.output.resolve(), args.exe.resolve(strict=True)
    baseline = args.baseline.resolve(strict=True) if args.baseline else None
    if baseline:
        baseline_report = json.loads((baseline/'report.json').read_text())
        if baseline_report.get('status') != 'passed' or not baseline_report.get('assessment', {}).get('passed'):
            parser.error('Baseline must have a passing geometry assessment')
        for name in ('SecondaryRayData', 'PrimaryContinuation', 'Albedo'):
            if not (baseline/(name+'.ptbuf')).is_file():
                parser.error('Missing baseline resource: '+name)
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    source = ROOT/'Assets/Scenes/PathTracingValidation/two-surface-indirect'
    scene = json.loads((source/'scene.json').read_text())
    for material in scene['materials']:
        material.update(metallic=.35, roughness=.55)
    scene['renderPreset'] = 'preset.json'
    preset = json.loads((source/'render-preset.json').read_text())
    preset['pathTracing'].update(accumulate=False, samplesPerFrame=1, pixelCenterPrimary=True,
        maxBounces=2, randomSeed=7, debugOutput=3, russianRouletteEnabled=False,
        directLightingEnabled=False, emissiveEnabled=False, environmentEnabled=True)
    preset['shadow'].update(rayTMin=.001, rayTMax=100., normalBias=.001)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    report = dict(schemaVersion=1, status='running', records=[], failures=[], exeSha256=sha(exe),
        shaderBytecodeSha256=sha(exe.parent/'shaders_PathTracing_CSMain.cso'),
        scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'))
    buffers = {}
    try:
        for name in RESOURCES:
            path, log = output/(name+'.ptbuf'), output/(name+'.log')
            command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
                '-EnablePathTracing', '-DebugPreviewResource', 'PathTracing.'+name, '-CapturePath', str(path),
                '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
            child = subprocess.Popen(command, cwd=ROOT)
            try:
                if child.wait(timeout=180) != 0:
                    raise RuntimeError('Application failed: '+name)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            text = log.read_text(encoding='utf-8-sig')
            if '[ERROR]' in text or '[CORRUPTION]' in text:
                raise RuntimeError('D3D12 capture error: '+name)
            buffers[name] = read_buffer(path)
            meta = buffers[name][0]
            if not (abs(meta['rayTMin']-.001) < 1e-9 and meta['rayTMax'] == 100 and
                    abs(meta['normalBias']-.001) < 1e-9):
                raise ValueError('Requested ray parameters were not applied')
            record = dict(resource=name, sha256=sha(path), command=command, d3d12Errors=0)
            if baseline and name != 'SecondarySurfaceDistance':
                old_meta, old_values = read_buffer(baseline/(name+'.ptbuf'))
                if meta != old_meta or not np.array_equal(buffers[name][1], old_values):
                    raise ValueError('Legacy baseline changed: '+name)
                record['legacyBaselineIdentical'] = True
                record['baselineSha256'] = sha(baseline/(name+'.ptbuf'))
            report['records'].append(record)
            write_json(output/'report.json', report)
            print('Captured '+name, flush=True)
        report['assessment'] = assess(buffers, scene)
        if not report['assessment']['passed']:
            raise ValueError('Independent geometry check failed')
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
