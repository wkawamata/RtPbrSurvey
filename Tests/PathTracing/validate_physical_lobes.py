"""Check physical PT component reconstruction and an independent mixed-BRDF oracle."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer, sample_positions
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('PhysicalDiffuse', 'PhysicalSpecular', 'CurrentFrameRadiance',
             'StableRadiance', 'BackgroundRadiance', 'PrimaryContinuation',
             'NormalRoughness', 'Albedo', 'SecondaryRayData')


def diffuse_fraction(albedo, metallic, roughness, normal, view, light):
    """Double-precision component evaluation, independent of proposal identity."""
    half = view + light
    half /= np.linalg.norm(half, axis=-1, keepdims=True)
    nv = np.clip(np.sum(normal*view, axis=-1), 0, 1)
    nl = np.clip(np.sum(normal*light, axis=-1), 0, 1)
    nh = np.clip(np.sum(normal*half, axis=-1), 0, 1)
    vh = np.clip(np.sum(view*half, axis=-1), 0, 1)
    alpha2 = np.maximum(roughness**2, .001)**2
    distribution = alpha2/np.maximum(np.pi*(nh*nh*(alpha2-1)+1)**2, 1e-6)
    def masking(cosine):
        return 2*cosine/np.maximum(cosine + np.sqrt(alpha2+(1-alpha2)*cosine**2), 1e-30)
    f0 = .04*(1-metallic) + albedo*metallic
    fresnel = f0 + (1-f0)*(1-vh[..., None])**5
    diffuse = (1-fresnel)*(1-metallic)*albedo/np.pi
    specular = (distribution*masking(nv)*masking(nl)/np.maximum(4*nv*nl, 1e-6))[..., None]*fresnel
    return np.divide(diffuse, diffuse+specular, out=np.zeros_like(diffuse), where=diffuse+specular > 0)


def assess(buffers):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected physical signal resources')
    meta = buffers['CurrentFrameRadiance'][0]
    if meta.get('samplesPerFrame', 1) != 1:
        raise ValueError('Physical oracle requires one primary sample per pixel')
    for name in RESOURCES:
        other, values = buffers[name]
        expected_format = 10 if name in ('NormalRoughness', 'Albedo') else 2
        if other['resource'] != 'PathTracing.'+name or other['format'] != expected_format:
            raise ValueError('Wrong resource: '+name)
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex', 'viewProjection',
                    'previousViewProjection', 'inverseViewProjection'):
            if other[key] != meta[key]:
                raise ValueError('Cohort mismatch: '+key)
        if other.get('primarySampling', 'random-per-pixel') != meta.get('primarySampling', 'random-per-pixel'):
            raise ValueError('Cohort mismatch: primarySampling')
        if other.get('samplesPerFrame', 1) != 1:
            raise ValueError('Cohort mismatch: samplesPerFrame')
        if values.shape != (meta['height'], meta['width'], 4) or not np.isfinite(values).all():
            raise ValueError('Invalid signal: '+name)
    arrays = {name: buffers[name][1] for name in RESOURCES}
    diffuse, specular = arrays['PhysicalDiffuse'], arrays['PhysicalSpecular']
    expected = arrays['CurrentFrameRadiance'][..., :3]
    reconstructed = diffuse[..., :3]+specular[..., :3]+arrays['StableRadiance'][..., :3]+arrays['BackgroundRadiance'][..., :3]
    error = abs(reconstructed-expected)
    checks = dict(reconstruction=bool((error <= 2e-5+2e-5*abs(expected)).all()),
                  nonnegative=bool((diffuse >= 0).all() and (specular >= 0).all()),
                  reserved=bool((diffuse[..., 3] == 0).all() and (specular[..., 3] == 0).all()))
    ys, xs = np.mgrid[8:meta['height']-8:8, 8:meta['width']-8:8]
    xs, ys = xs.ravel(), ys.ravel()
    continuation = arrays['PrimaryContinuation'][ys, xs]
    secondary = arrays['SecondaryRayData'][ys, xs]
    total = diffuse[ys, xs, :3]+specular[ys, xs, :3]
    mask = (secondary[:, 2] == 2) & (continuation[:, 3] > 0) & (total.min(axis=-1) > 1e-6)
    xs, ys, continuation, secondary, total = xs[mask], ys[mask], continuation[mask], secondary[mask], total[mask]
    if len(xs) < 100:
        raise ValueError('Insufficient independently evaluated continuation misses')
    u, v = sample_positions(meta, xs, ys)
    ndc = np.column_stack((2*(xs+u)/meta['width']-1, 1-2*(ys+v)/meta['height']))
    inverse = np.asarray(meta['inverseViewProjection']).T
    near = np.column_stack((ndc, np.zeros(len(xs)), np.ones(len(xs)))) @ inverse
    far = np.column_stack((ndc, np.ones(len(xs)), np.ones(len(xs)))) @ inverse
    view = near[:, :3]/near[:, 3, None]-far[:, :3]/far[:, 3, None]
    view /= np.linalg.norm(view, axis=-1, keepdims=True)
    normal_roughness = arrays['NormalRoughness'][ys, xs]
    normal = normal_roughness[:, :3]
    normal_length = np.linalg.norm(normal, axis=-1, keepdims=True)
    if (normal_length < 1e-6).any():
        raise ValueError('Invalid primary normal')
    normal = normal / normal_length
    fraction = diffuse_fraction(arrays['Albedo'][ys, xs, :3], .35,
        normal_roughness[:, 3], normal, view, continuation[:, :3])
    observed = diffuse[ys, xs, :3]/total
    fraction_error = abs(observed-fraction)
    checks['mixedFraction'] = bool((fraction_error <= .003).all())
    checks['bothProposals'] = bool((secondary[:, 1] == 1).any() and (secondary[:, 1] == 2).any())
    checks['bothPhysicalComponents'] = bool((observed > 0).all() and (observed < 1).all())
    checks['unitDirections'] = bool((abs(np.linalg.norm(continuation[:, :3], axis=-1)-1) < 1e-5).all())
    return dict(passed=all(checks.values()), checks=checks,
                reconstructionMaxError=float(error.max()), fractionMaxError=float(fraction_error.max()),
                oraclePixels=len(xs), tolerance=.003,
                limitation='Single-sample two-segment mixed-material environment misses; no NRD packing/dispatch.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pixel-center', action='store_true')
    args = parser.parse_args()
    exe, output = args.exe.resolve(strict=True), args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    source = ROOT/'Assets/Scenes/PathTracingValidation/two-surface-indirect'
    scene = json.loads((source/'scene.json').read_text())
    for material in scene['materials']:
        material.update(metallic=.35, roughness=.55)
    scene['renderPreset'] = 'preset.json'
    preset = json.loads((source/'render-preset.json').read_text())
    preset['pathTracing'].update(accumulate=False, samplesPerFrame=1, randomSeed=7,
        pixelCenterPrimary=args.pixel_center,
        maxBounces=2, directLightingEnabled=False, emissiveEnabled=False,
        environmentEnabled=True, environmentSamplingMode=0, russianRouletteEnabled=False, debugOutput=3)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    report = dict(schemaVersion=1, status='running', records=[], failures=[], exeSha256=sha(exe),
        shaderSha256=sha(ROOT/'Shaders/shaders_PathTracing.hlsl'), scriptSha256=sha(Path(__file__)),
        sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'))
    report['primarySampling'] = 'pixel-center' if args.pixel_center else 'random-per-pixel'
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
                raise RuntimeError('D3D12 capture errors: '+name)
            buffers[name] = read_buffer(path)
            if buffers[name][0].get('primarySampling') != report['primarySampling']:
                raise ValueError('Requested primary sampling was not applied')
            report['records'].append(dict(resource=name, sha256=sha(path), command=command, d3d12Errors=0))
            write_json(output/'report.json', report)
            print('Captured '+name, flush=True)
        report['assessment'] = assess(buffers)
        if not report['assessment']['passed']:
            raise ValueError('Physical component checks failed')
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
