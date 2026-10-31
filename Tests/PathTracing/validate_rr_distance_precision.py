"""Separate primary unprojection precision from first-continuation distance errors."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_secondary_geometry import RESOURCES, assess as assess_geometry
from validate_secondary_geometry import primary_rays, planes_from_scene, intersect_planes


def float_primary_positions(meta, xs, ys, planes):
    # FP32 model, not a bit-exact GPU emulator: FMA/normalization may differ.
    uv = (np.column_stack((xs, ys)).astype(np.float32)+np.float32(.5)) / np.asarray(
        [meta['width'], meta['height']], dtype=np.float32)
    ndc = uv*np.float32(2)-np.float32(1)
    ndc[:, 1] = -ndc[:, 1]
    matrix = np.asarray(meta['inverseViewProjection'], dtype=np.float32).T
    zeros, ones = np.zeros(len(xs), dtype=np.float32), np.ones(len(xs), dtype=np.float32)
    near = np.column_stack((ndc, zeros, ones)) @ matrix
    far = np.column_stack((ndc, ones, ones)) @ matrix
    near, far = near[:, :3]/near[:, 3, None], far[:, :3]/far[:, 3, None]
    direction = far-near
    direction /= np.linalg.norm(direction, axis=-1, keepdims=True)
    distance, _, _ = intersect_planes(near, direction, planes, meta['rayTMin'], meta['rayTMax'])
    return (near+direction*distance[:, None]).astype(np.float32)


def assess(buffers, primary, scene):
    original = assess_geometry(buffers, scene)
    meta = buffers['Albedo'][0]
    other, positions = primary
    if (other['resource'] != 'PathTracing.DenoiserPrimaryPosition' or other['format'] != 2
            or positions.shape != (meta['height'], meta['width'], 4) or not np.isfinite(positions).all()
            or {k: v for k, v in other.items() if k not in ('resource', 'format')} !=
               {k: v for k, v in meta.items() if k not in ('resource', 'format')}):
        raise ValueError('Primary-position cohort mismatch')
    planes = planes_from_scene(scene)
    ys, xs = np.mgrid[8:meta['height']-8:4, 8:meta['width']-8:4]
    xs, ys = xs.ravel(), ys.ravel()
    origins, directions = primary_rays(meta, xs, ys)
    distance, indices, edge = intersect_planes(origins, directions, planes, meta['rayTMin'], meta['rayTMax'])
    secondary = buffers['SecondaryRayData'][1][ys, xs]
    eligible = (indices >= 0) & (secondary[:, 2] != 0) & ~edge
    xs, ys = xs[eligible], ys[eligible]
    double_position = origins[eligible]+directions[eligible]*distance[eligible, None]
    normals = np.asarray([planes[i][3] for i in indices[eligible]])
    gpu_position = positions[ys, xs, :3]
    float_position = float_primary_positions(meta, xs, ys, planes)
    continuation = buffers['PrimaryContinuation'][1][ys, xs, :3]
    observed = secondary[eligible]
    surface = buffers['SecondarySurfaceDistance'][1][ys, xs]
    models = {}
    worst = []
    for name, position in (('doublePrimary', double_position), ('floatPrimary', float_position),
                           ('capturedPrimary', gpu_position)):
        # Match the producer's FP32 origin addition for the GPU-origin check.
        if name == 'doublePrimary':
            origin = position+meta['normalBias']*normals
        else:
            origin = (position.astype(np.float32)+np.float32(meta['normalBias'])*normals.astype(np.float32))
        offset = origin.astype(float)-position
        expected, hit_index, boundary = intersect_planes(origin, continuation, planes, meta['rayTMin'], meta['rayTMax'])
        selected = ~boundary
        hit = (hit_index >= 0) & selected
        expected = np.where(hit_index >= 0, expected, meta['rayTMax'])
        error = abs(observed[:, 0]-expected)
        bound = 1e-4+5e-5*abs(expected)
        physical = np.linalg.norm(offset[hit]+continuation[hit]*expected[hit, None], axis=-1)
        physical_error = abs(surface[hit, 0]-physical)
        checks = dict(states=bool(np.array_equal(observed[selected, 2], np.where(hit_index[selected] >= 0, 1, 2))),
                      queryDistance=bool((error[selected] <= bound[selected]).all()),
                      surfaceDistance=bool((physical_error <= bound[hit]).all()),
                      coverage=bool(hit.sum() >= 100 and (selected & ~hit).sum() >= 100))
        models[name] = dict(passed=all(checks.values()), checks=checks, hitPixels=int(hit.sum()),
                            failedQueryPixels=int((selected & (error > bound)).sum()),
                            maxQueryError=float(error[hit].max()), maxSurfaceError=float(physical_error.max()),
                            maxBoundRatio=float((error[selected]/bound[selected]).max()))
        if name == 'doublePrimary':
            for j in np.argsort(np.where(hit, error/bound, 0))[-8:][::-1]:
                worst.append(dict(x=int(xs[j]), y=int(ys[j]), error=float(error[j]),
                                  secondaryNormalDot=float(np.dot(continuation[j], planes[hit_index[j]][3])),
                                  primaryPositionDifference=(gpu_position[j]-position[j]).tolist()))
    upstream = {key: value for key, value in original['checks'].items()
                if key not in ('secondaryDistance', 'reconstructedSurfaceDistance', 'publishedSurfaceDistance')}
    checks = dict(upstream=all(upstream.values()), primaryValid=bool((positions[ys, xs, 3] == 1).all()),
                  capturedOriginDistance=models['capturedPrimary']['passed'],
                  floatPrimaryDistance=models['floatPrimary']['passed'])
    return dict(passed=all(checks.values()), checks=checks, originalDoubleOracle=original, models=models,
                primaryPositionMaxErrorDouble=float(abs(gpu_position-double_position).max()),
                primaryPositionMaxErrorFloat=float(abs(gpu_position-float_position).max()), worstDoublePixels=worst,
                tolerance='Unchanged: 1e-4 + 5e-5 * expected distance', nativeEvaluationQualified=False,
                limitation='Captured-origin conditional geometry check, not an independent validation of GPU primary unprojection. FP32 model is not bit-exact GPU emulation. Finite planes and first continuation only.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--primary', type=Path)
    mode.add_argument('--exe', type=Path)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Use a separate empty output directory')
    scene = json.loads((source/'scene.json').read_text())
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', json.loads((source/'preset.json').read_text()))
    (output/'validation-script.py').write_bytes(Path(__file__).read_bytes())
    report = dict(status='running', failures=[], sourceReportSha256=sha(source/'report.json'),
                  scriptSha256=sha(Path(__file__)), nativeEvaluationQualified=False)
    try:
        buffers = {name: read_buffer(source/(name+'.ptbuf')) for name in RESOURCES}
        report['sourceBuffers'] = {name: sha(source/(name+'.ptbuf')) for name in RESOURCES}
        if args.exe:
            exe = args.exe.resolve(strict=True)
            source_report = json.loads((source/'report.json').read_text())
            if (sha(exe) != source_report['exeSha256'] or
                    sha(exe.parent/'shaders_PathTracing_CSMain.cso') != source_report['producerShaderSha256']):
                raise ValueError('Producer executable/shader differs from source cohort')
            primary, record = capture(exe, output, 'DenoiserPrimaryPosition', output/'preset.json', 'primary')
            report['capture'] = record
        else:
            primary = read_buffer(args.primary.resolve(strict=True))
            report['primarySha256'] = sha(args.primary)
        report['assessment'] = assess(buffers, primary, scene)
        if not report['assessment']['passed']:
            raise ValueError('Conditional distance precision check failed')
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
