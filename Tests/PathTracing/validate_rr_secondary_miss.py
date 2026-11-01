"""Verify one retained live RR production and its actual continuation classes."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_rr_native import fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--profile', choices=('mirror-miss', 'general', 'dielectric', 'untraced', 'mixed-background'), default='mirror-miss')
    parser.add_argument('--max-bounces', type=int, choices=range(1, 17), default=2)
    parser.add_argument('--accumulate', action='store_true')
    parser.add_argument('--russian-roulette', action='store_true')
    parser.add_argument('--samples-per-frame', type=int, choices=range(1, 17), default=1)
    parser.add_argument('--frame-jitter', action='store_true')
    args = parser.parse_args()
    source, output, exe = args.source.resolve(strict=True), args.output.resolve(), args.exe.resolve(strict=True)
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    fixture(source, output)
    gltf = json.loads((output/'box.gltf').read_text(encoding='utf-8'))
    if args.profile == 'mirror-miss':
        gltf['scenes'][0]['nodes'] = [1]
    elif args.profile == 'mixed-background':
        gltf['scenes'][0]['nodes'] = [1]
        gltf['nodes'][1]['scale'] = [.3, .3, 1]
    if args.profile != 'mirror-miss':
        gltf['materials'][0]['pbrMetallicRoughness'].update(
            metallicFactor=1 if args.profile == 'untraced' else 0 if args.profile == 'dielectric' else .5,
            roughnessFactor=1 if args.profile == 'untraced' else .5)
    if args.max_bounces == 1:
        gltf['materials'][0]['emissiveFactor'] = [.1, .05, .025]
    write_json(output/'box.gltf', gltf)
    preset = json.loads((output/'preset.json').read_text(encoding='utf-8'))
    preset['pathTracing'].update(environmentEnabled=True, environmentSamplingMode=5,
                                 maxBounces=args.max_bounces, accumulate=args.accumulate,
                                 russianRouletteEnabled=args.russian_roulette, samplesPerFrame=args.samples_per_frame,
                                 pixelCenterPrimary=not args.frame_jitter)
    preset['lighting']['iblIntensity'] = 1
    write_json(output/'preset.json', preset)
    log = output/'runtime.log'
    command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
               '-WindowSize', '256', '144', '-EnableDlssRayReconstruction',
               '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrInputCapture', str(output),
               '-LogToFile', str(log)]
    report = dict(status='running', command=command, executableSha256=sha(exe),
                  conversionShaderSha256=sha(exe.parent/'shaders_PathTracingRrConversion_CSMain.cso'), failures=[])
    write_json(output/'report.json', report)
    try:
        child = subprocess.Popen(command, cwd=Path(__file__).resolve().parents[2])
        try:
            code = child.wait(timeout=180)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
        report['exitCode'] = code
        text = log.read_text(encoding='utf-8-sig')
        errors = [line for line in text.splitlines()
                  if line.startswith(('[ERROR]', '[CORRUPTION]', '[PathTracingRrInputCaptureFailure]'))]
        if code != 0 or errors:
            raise ValueError(f'Process={code}, errors={errors}')
        prefix = '[PathTracingRrInputCaptureComplete] '
        records = [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]
        if len(records) != 1 or records[0]['nativeCalls'] != 1 or not records[0]['verified'] or records[0]['captures'] != 8:
            raise ValueError('One verified RR evaluation and eight completed captures are required')
        names = ('SecondaryRayData', 'SecondarySurfaceDistance', 'RrHitDistance', 'RrColor',
                 'CurrentFrameRadiance', 'DenoiserPrimaryPosition', 'RrOutput', 'RrMotion')
        images, stamps, hashes = {}, [], {}
        for name in names:
            path = output/(name+'.ptbuf')
            meta, values = read_buffer(path)
            key = meta['rrRetainedInput']
            if (meta['resource'] != 'PathTracing.'+name or meta['productionFrameId'] != key['frameId'] or
                    key['frameId'] != records[0]['frameId'] or key['publishedFrameId'] != key['frameId'] or
                    key['publishedSlot'] != 0 or meta['sampleStartIndex'] != key['sampleStartIndex'] + args.samples_per_frame or
                    key['samplesPerFrame'] != args.samples_per_frame or
                    (meta['width'], meta['height']) != (256, 144) or
                    meta['format'] != (41 if name == 'RrHitDistance' else 16 if name == 'RrMotion' else 2)):
                raise ValueError('Wrong or unverified retained capture: '+name)
            stamps.append(key)
            images[name] = values
            hashes[name] = sha(path)
        if any(key != stamps[0] for key in stamps[1:]):
            raise ValueError('Raw guides and output mixed productions/cameras/samples')
        ray, surface = images['SecondaryRayData'], images['SecondarySurfaceDistance']
        if float(np.abs(images['RrMotion']).max()) > 2e-6:
            raise ValueError('Static camera has residual projection-jitter motion')
        jitter = np.asarray(stamps[0]['jitterPixels'])
        if not np.array_equal(jitter, stamps[0]['sdkJitterPixels']):
            raise ValueError('SDK jitter does not match the retained production')
        if args.frame_jitter != bool(np.abs(jitter).max() > 0):
            raise ValueError('Retained projection jitter does not match requested mode')
        primary = images['DenoiserPrimaryPosition']
        background = primary[..., 3] == 0
        untraced = (ray == 0).all(axis=-1) & ~background
        specular = (ray[..., 1] == 2) & ~background
        hit = ray[..., 2] == 1
        miss = ray[..., 2] == 2
        diffuse = (ray[..., 1] == 1) & ~background
        if not ((background | untraced | specular | diffuse).all() and
                ((surface[..., 2:] == 0).all()) and (ray[..., 3] == 0).all()):
            raise ValueError('Invalid raw continuation class')
        if not ((ray[..., 2] == surface[..., 1]).all() and
                (surface[..., 0][miss] == 0).all() and (surface[..., 0][hit] > 0).all()):
            raise ValueError('Raw trace state/distance mismatch')
        traced = specular | diffuse
        if not ((ray[..., 0][traced] > 0).all() and (hit | miss)[traced].all() and
                (ray[background] == 0).all() and (surface[background | untraced] == 0).all()):
            raise ValueError('Absent input or traced ray state is inconsistent')
        expected_distance = np.where(specular & hit, surface[..., 0],
                                     np.where(specular & miss, 65504, 0))
        if not np.array_equal(images['RrHitDistance'][..., 0], expected_distance):
            raise ValueError('Specular hit/miss/absent guide distance mismatch')
        counts = dict(background=int(background.sum()), untraced=int(untraced.sum()),
                      specular=int(specular.sum()), diffuse=int(diffuse.sum()))
        if args.max_bounces > 1 and args.profile == 'mirror-miss' and not (specular & miss).all():
            raise ValueError('Expected only actual mirror misses')
        if args.max_bounces > 1 and args.profile in ('general', 'dielectric') and not (counts['specular'] and counts['diffuse']):
            raise ValueError('General fixture did not exercise both sampled lobes')
        if args.profile == 'untraced' and not counts['untraced']:
            raise ValueError('Fixture did not exercise actual untraced samples')
        if args.max_bounces > 1 and args.profile == 'mixed-background' and not (counts['background'] and counts['specular'] and counts['diffuse']):
            raise ValueError('Mixed fixture lacks background or a material lobe')
        if args.max_bounces == 1 and not untraced[~background].all():
            raise ValueError('One-bounce input invented a traced continuation')
        clip = primary @ np.asarray(stamps[0]['viewProjection']).reshape(4, 4)
        height, width = primary.shape[:2]
        y, x = np.mgrid[:height, :width]
        expected_ndc = np.stack(((x+.5)*2/width-1, 1-(y+.5)*2/height), axis=-1)
        expected_ndc -= jitter * [2/width, -2/height]
        surface_pixels = ~background
        projection_error = float(np.abs(clip[..., :2][surface_pixels]/clip[..., 3:][surface_pixels]-
                                        expected_ndc[surface_pixels]).max())
        if not np.isfinite(projection_error) or projection_error > 2e-5:
            raise ValueError('Retained camera does not project primary hits to their pixels')
        if not np.array_equal(images['RrColor'], images['CurrentFrameRadiance']):
            raise ValueError('RR color changed the retained scene-referred radiance')
        if images['RrOutput'][..., :3].max() <= 0 or images['RrColor'][..., :3].max() <= 0:
            raise ValueError('Constant environment reflection is black')
        report.update(status='passed', completion=records[0], retainedInput=stamps[0], captures=hashes,
                      profile=args.profile, maxBounces=args.max_bounces, accumulate=args.accumulate,
                      samplesPerFrame=args.samples_per_frame, frameJitter=args.frame_jitter,
                      staticMotionMaxAbs=float(np.abs(images['RrMotion']).max()),
                      russianRoulette=args.russian_roulette, classifications=counts, primaryProjectionError=projection_error, d3d12Errors=0, qualityVerdict='not-evaluated')
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failures=report['failures'])), flush=True)
    if report['status'] != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
