"""Capture native PT guide values and compare independently projected plane hits."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone
from validate_part1 import write_json, sha

ROOT = Path(__file__).resolve().parents[2]
FORMATS = {10: ('<f2', 4), 28: ('u1', 4), 34: ('<f2', 2), 41: ('<f4', 1)}
RESOURCES = ['NormalRoughness', 'ViewZ', 'MotionVectors', 'Albedo']


def read_buffer(path):
    import numpy as np
    with path.open('rb') as stream:
        if stream.readline() != b'PTBUF1\n':
            raise ValueError('Invalid PTBUF magic')
        meta = json.loads(stream.readline())
        if meta.get('schemaVersion') != 1 or meta.get('rowOrder') != 'top-down':
            raise ValueError('Unsupported PTBUF schema or row order')
        if meta['format'] not in FORMATS:
            raise ValueError('Unsupported native format')
        dtype, channels = FORMATS[meta['format']]
        width, height = meta['width'], meta['height']
        if not isinstance(width, int) or not isinstance(height, int) or min(width, height) <= 0:
            raise ValueError('Invalid dimensions')
        data = stream.read()
        if len(data) != width*height*channels*np.dtype(dtype).itemsize:
            raise ValueError('Truncated or trailing PTBUF data')
        values = np.frombuffer(data, dtype=dtype).reshape(height, width, channels).astype(np.float64)
        if meta['format'] == 28:
            values /= 255
        if not np.isfinite(values).all():
            raise ValueError('Nonfinite guide values')
        return meta, values


def hash_uint(value):
    import numpy as np
    value = np.asarray(value, dtype=np.uint32)
    value = value ^ (value >> 16)
    value = ((value.astype(np.uint64) * 0x7feb352d) & 0xffffffff).astype(np.uint32)
    value = value ^ (value >> 15)
    value = ((value.astype(np.uint64) * 0x846ca68b) & 0xffffffff).astype(np.uint32)
    return value ^ (value >> 16)


def sample_positions(meta, xs, ys):
    import numpy as np
    add = np.uint32(0x9e3779b9)
    seed = np.uint32((meta['randomSeed'] + int(add)) & 0xffffffff)
    state = hash_uint(np.asarray(xs, dtype=np.uint32) ^ hash_uint(
        np.asarray(ys, dtype=np.uint32) ^ hash_uint(meta['sampleStartIndex']) ^ hash_uint(seed)))
    state = hash_uint(state + add)
    u = (state >> 8).astype(np.float64)/16777216
    state = hash_uint(state + add)
    v = (state >> 8).astype(np.float64)/16777216
    return u, v


def project(points, stored_transposed_matrix):
    import numpy as np
    clips = np.column_stack((points, np.ones(len(points)))) @ np.asarray(stored_transposed_matrix).T
    return clips[:, :3] / clips[:, 3, None]


def plane_hits(meta, xs, ys, marker=False):
    import numpy as np
    u, v = sample_positions(meta, xs, ys)
    ndc = np.column_stack((2*(xs+u)/meta['width']-1, 1-2*(ys+v)/meta['height']))
    inv = np.asarray(meta['inverseViewProjection']).T
    near = np.column_stack((ndc, np.zeros(len(xs)), np.ones(len(xs)))) @ inv
    far = np.column_stack((ndc, np.ones(len(xs)), np.ones(len(xs)))) @ inv
    near = near[:, :3]/near[:, 3, None]
    far = far[:, :3]/far[:, 3, None]
    direction = far-near
    world = near + direction*(-near[:, 2]/direction[:, 2])[:, None]
    material = np.zeros(len(xs), dtype=int)
    if marker:
        front = near + direction*((-1-near[:, 2])/direction[:, 2])[:, None]
        chosen = (abs(front[:, 0]-.5) < 1) & (abs(front[:, 1]-.25) < 1)
        world[chosen] = front[chosen]
        material[chosen] = 1
    return world, material, ndc


def srgb_texture_material(base, roughness):
    import numpy as np
    base = np.asarray(base)
    encoded = np.where(base <= .0031308, 12.92*base, 1.055*base**(1/2.4)-.055)
    bytes_ = np.floor(encoded*255+.5)
    sampled = bytes_/255
    decoded = np.where(sampled <= .04045, sampled/12.92, ((sampled+.055)/1.055)**2.4)
    return decoded, roughness, bytes_.astype(int).tolist()


def projection_plane_forward(stored_inverse):
    import numpy as np
    points = np.array([[0, 0, 1, 1], [1, 0, 1, 1], [0, 1, 1, 1]]) @ np.asarray(stored_inverse).T
    points = points[:, :3]/points[:, 3, None]
    forward = np.cross(points[1]-points[0], points[2]-points[0])
    return forward/np.linalg.norm(forward)


def analyze(path, marker=False):
    import numpy as np
    meta, raw = read_buffer(path)
    # Interior quarter-grid: no UI values, no tone map, no accumulation normalization.
    xs, ys = np.meshgrid(np.arange(16, meta['width']-16, 4), np.arange(16, meta['height']-16, 4))
    xs, ys = xs.ravel(), ys.ravel()
    world, material, ndc = plane_hits(meta, xs, ys, marker)
    observed = raw[ys, xs]
    camera = np.asarray(meta['cameraPosition'])
    forward = np.asarray(meta['cameraTarget']) - camera
    forward /= np.linalg.norm(forward)
    center = np.array([0, 0, 1, 1]) @ np.asarray(meta['inverseViewProjection']).T
    off_axis_forward = center[:3]/center[3]-camera
    off_axis_forward /= np.linalg.norm(off_axis_forward)
    shader_forward = projection_plane_forward(meta['inverseViewProjection'])
    expected_axis_depth = (world-camera) @ forward
    expected_shader_depth = (world-camera) @ shader_forward
    decoded0, rough0, bytes0 = srgb_texture_material([.25, .5, .75], .37)
    decoded1, rough1, bytes1 = srgb_texture_material([.75, .25, .125], .8)
    resource = meta['resource'].split('.')[-1]
    if resource == 'NormalRoughness':
        expected = np.column_stack((np.tile([0, 0, -1], (len(xs), 1)), np.where(material == 0, rough0, rough1)))
        tolerance = .001
    elif resource == 'Albedo':
        expected = np.column_stack((np.where(material[:, None] == 0, decoded0, decoded1), np.ones(len(xs))))
        tolerance = .0015  # hardware UNORM sampling, sRGB decode, half storage
    elif resource == 'MotionVectors':
        expected = (project(world, meta['previousViewProjection']) - project(world, meta['viewProjection']))[:, :2]
        tolerance = .00002  # retain initial absolute threshold as a separate diagnostic
    else:
        expected = expected_axis_depth[:, None]
        tolerance = .0001  # float32 storage/geometry + shader arithmetic at 5 world units
    error = abs(observed-expected)
    result = dict(resource=resource, pixelCount=len(xs), maxAbsoluteError=float(error.max()),
        meanAbsoluteError=float(error.mean()), tolerance=tolerance, passed=bool(error.max() <= tolerance),
        observedMin=observed.min(axis=0).tolist(), observedMax=observed.max(axis=0).tolist(),
        expectedMin=expected.min(axis=0).tolist(), expectedMax=expected.max(axis=0).tolist(),
        matrixReprojectionMaxError=float(abs(project(world, meta['viewProjection'])[:, :2]-ndc).max()),
        sampleStartIndex=meta['sampleStartIndex'], cameraPosition=meta['cameraPosition'],
        cameraTarget=meta['cameraTarget'], lensShift=meta['lensShift'],
        textureBytes=[bytes0, bytes1], effectiveRoughness=[rough0, rough1])
    if resource == 'Albedo':
        approximate0 = (np.asarray(bytes0)/255)**2.2
        approximate1 = (np.asarray(bytes1)/255)**2.2
        shader_expected = np.column_stack((np.where(material[:, None] == 0, approximate0, approximate1), np.ones(len(xs))))
        shader_error = abs(observed-shader_expected)
        result.update(standardSrgbPassed=result['passed'],
            legacyGamma22MaxError=float(shader_error.max()),
            shaderDefinitionMaxError=float(error.max()),
            shaderDefinitionPassed=result['passed'],
            shaderExpectedMin=expected.min(axis=0).tolist(),
            shaderExpectedMax=expected.max(axis=0).tolist(),
            primaryHitMaterialMismatchCount=int(np.count_nonzero((np.linalg.norm(observed[:, :3]-decoded1, axis=1) <
                np.linalg.norm(observed[:, :3]-decoded0, axis=1)).astype(int) != material)) if marker else 0)
    if resource == 'ViewZ':
        shader_error = abs(observed[:, 0]-expected_shader_depth)
        result.update(shaderDefinitionMaxError=float(shader_error.max()),
            cameraAxisDefinitionPassed=result['passed'],
            legacyOffAxisDefinitionMaxError=float(abs(observed[:, 0]-(world-camera) @ off_axis_forward).max()),
            shaderDefinitionPassed=bool(shader_error.max() <= tolerance))
    if resource == 'MotionVectors':
        displacement = observed * [meta['width']/2, -meta['height']/2]
        # Format-derived bound, selected after the retained orbit pilot and before the final cohort.
        ulp = np.abs(np.spacing(np.abs(expected).astype(np.float16)).astype(np.float64))
        bound = 2*ulp + .000003
        result.update(pixelDisplacementMin=displacement.min(axis=0).tolist(), pixelDisplacementMax=displacement.max(axis=0).tolist(),
            absoluteThresholdPassed=result['passed'],
            halfPrecisionBound='2 ULP(expected half) + 3e-6 NDC float geometry/matrix allowance',
            halfPrecisionBoundMin=float(bound.min()), halfPrecisionBoundMax=float(bound.max()),
            halfPrecisionBoundPassed=bool((error <= bound).all()),
            maxBoundNormalizedError=float((error/bound).max()))
    return meta, result


def require_numeric_validation(result):
    check = 'halfPrecisionBoundPassed' if result['resource'] == 'MotionVectors' else 'passed'
    if not result.get(check, False):
        raise RuntimeError(f"{result['resource']} numeric validation failed: {check}")
    if result.get('primaryHitMaterialMismatchCount', 0) != 0:
        raise RuntimeError('Primary-hit material classification failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/part5-inputs')
    parser.add_argument('--analyze-only', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--cases', help='Comma-separated capture names')
    parser.add_argument('--seed', type=int, default=7)
    parser.add_argument('--base-commit', default='9455eec', help='Recorded validation baseline commit')
    args = parser.parse_args()
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    plan = [(s, False, resource) for s in ['input-plane', 'input-shifted', 'input-ortho', 'input-marker'] for resource in RESOURCES]
    plan += [(s, True, 'MotionVectors') for s in ['input-plane', 'input-shifted', 'input-ortho', 'input-marker']]
    if args.smoke:
        plan = plan[:1]
    if args.cases:
        requested = set(args.cases.split(','))
        def capture_name(item):
            return f'{item[0]}-{item[2]}-' + ('moving' if item[1] else 'static')
        available = plan + [('input-shifted', True, 'ViewZ'), ('input-camera-transform', False, 'ViewZ')]
        unknown = requested - {capture_name(item) for item in available}
        if unknown:
            parser.error('Unknown capture names: '+str(sorted(unknown)))
        plan = [item for item in available if capture_name(item) in requested]
    if not 0 <= args.seed <= 0xffffffff:
        parser.error('Seed must be uint32')
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(), baseCommit=args.base_commit,
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        workspace=str(ROOT), build='Debug x64', samplesPerFrame=1, seed=args.seed, captureAfterFrames=30,
        roi='x/y from 16 to dimension-16 exclusive, stride 4', records=[], failures=[],
        dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        executableSha256=sha(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'),
        sourceSha256={p: sha(ROOT/p) for p in ['App/RtPbrSurveyApp.cpp', 'Engine/RtPbrSurveyEngine.cpp',
            'Renderer/ScreenshotCapture.cpp', 'Scene/SceneDocumentBuilder.cpp', 'Scene/SceneDocumentJson.cpp',
            'Shaders/shaders_PathTracing.hlsl', 'Shaders/SceneRayQuery.hlsli', 'Tests/PathTracing/validate_inputs.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip())
    for scene_id, moving, resource in plan:
        name = f'{scene_id}-{resource}-' + ('moving' if moving else 'static')
        path = args.output/(name+'.ptbuf')
        log = args.output/(name+'.log')
        directory = ROOT/'Assets/Scenes/PathTracingValidation'/scene_id
        command = [str(ROOT/'bin/x64/Debug/RtPbrSurvey.exe'), '-SceneFile', str(directory/'scene.json'),
            '-RenderPreset', str(directory/'render-preset.json'), '-EnablePathTracing', '-PathTracingSeed', str(args.seed),
            '-DebugPreviewResource', 'PathTracing.'+resource, '-CapturePath', str(path), '-CaptureAfterFrames', '30',
            '-LogToFile', str(log), '-ExitAfterCapture']
        if moving:
            command += ['-ReflectionOrbitDegrees', '8', '-ReflectionOrbitFrames', '8']
        record = dict(name=name, command=command, sceneSha256=sha(directory/'scene.json'),
            presetSha256=sha(directory/'render-preset.json'), moving=moving, path=str(path.relative_to(ROOT)))
        report['records'].append(record)
        try:
            if not args.analyze_only:
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
                try:
                    code = child.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                    raise
                if code != 0:
                    raise RuntimeError(f'App exit {code}')
            text = log.read_text(encoding='utf-8-sig')
            errors = [line for line in text.splitlines() if '[ERROR]' in line or '[CORRUPTION]' in line]
            if errors:
                raise RuntimeError('D3D12 errors: '+str(errors[:2]))
            meta, result = analyze(path, scene_id == 'input-marker')
            if moving and meta['viewProjection'] == meta['previousViewProjection']:
                raise RuntimeError('Requested orbit did not produce measurable camera motion')
            record.update(metadata=meta, result=result, sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
            print(name, json.dumps(result), flush=True)
            require_numeric_validation(result)
        except Exception as error:
            record['failure'] = str(error)
            report['failures'].append(dict(name=name, error=str(error)))
            print(name, 'FAILED', error, flush=True)
        write_json(args.output/'report.json', report)
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(args.output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
