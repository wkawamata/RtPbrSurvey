"""Validate live emissive composition and opaque Unlit/background Native fallback."""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np

from validate_denoiser_distances import capture
from validate_nrd_composition import NAMES, assess_composition
from validate_nrd_presentation import RELAX
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'Assets/Scene/PathTracingValidation/13-nrd-stable-radiance'
RESOURCES = NAMES + ('Albedo', 'DenoiserPrimaryPosition',
    'DenoiserDiffuseDistance', 'DenoiserSpecularDistance')
COLORS = ((.65, .2, .15), (.2, .4, .6), (.6, .5, .2))
EMISSION = (.8, .3, .1)


def assess_stable(buffers, minimum=100):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected stable-radiance resources')
    albedo = buffers['Albedo']
    if albedo.ndim != 3 or albedo.shape[-1] != 4 or any(
            image.shape != albedo.shape or not np.isfinite(image).all() for image in buffers.values()):
        raise ValueError('Malformed stable-radiance cohort')
    hit = albedo[..., 3] == 1
    masks = [hit & np.isclose(albedo[..., :3], color, atol=.001, rtol=0).all(axis=-1) for color in COLORS]
    emissive, unlit, ordinary = masks
    background = ~hit
    if min(int(mask.sum()) for mask in (*masks, background)) < minimum:
        raise ValueError('Insufficient emissive/Unlit/ordinary/background coverage')
    if not np.array_equal(emissive | unlit | ordinary, hit):
        raise ValueError('Unknown primary material')
    stable = buffers['StableRadiance'][..., :3]
    expected = np.zeros_like(stable)
    expected[emissive] = EMISSION
    expected[unlit] = COLORS[1]
    if not np.allclose(stable, expected, atol=1e-6, rtol=0):
        raise ValueError('Stable emission/Unlit is missing, duplicated or contaminated')
    if ((buffers['DenoiserPrimaryPosition'][unlit, 3] != 1).any() or
            any((buffers[name][unlit] != 0).any() for name in
                ('DenoiserDiffuseDistance', 'DenoiserSpecularDistance',
                 'PhysicalDiffuse', 'PhysicalSpecular'))):
        raise ValueError('Unlit must retain its primary hit but have no PBR signal or distance inputs')
    if ((buffers['NrdInputAdmission'][unlit, 0] != 0).any() or
            (buffers['NrdValidity'][unlit, 0] != 0).any()):
        raise ValueError('Unlit entered the PBR denoiser')
    native, composed = buffers['SceneColor'], buffers['NrdSceneColor']
    if not np.allclose(native[unlit, :3], COLORS[1], atol=.001, rtol=0):
        raise ValueError('Native Unlit includes lighting or emission')
    for mask in (unlit, background):
        if not np.array_equal(composed[mask], native[mask]):
            raise ValueError('Unlit/background Native fallback is not exact')
    bg = buffers['BackgroundRadiance'][..., :3]
    if (bg[hit] != 0).any() or not (bg[background] > 0).any():
        raise ValueError('Background radiance is missing or leaks onto a primary surface')
    used = buffers['NrdCompositionDiagnostics'][..., 0] == 1
    if min(int((used & mask).sum()) for mask in (emissive, ordinary)) < minimum:
        raise ValueError('Emissive and ordinary materials must actually exercise NRD composition')
    result = assess_composition(buffers)
    result.update(emissivePixels=int(emissive.sum()), unlitPixels=int(unlit.sum()),
        ordinaryPixels=int(ordinary.sum()), backgroundPixels=int(background.sum()),
        composedEmissivePixels=int((used & emissive).sum()),
        unlitFallbackComparator='exact decoded Native', stableBound='absolute 1e-6')
    return result


def check_metadata(meta, name, common):
    expected_format = 10 if name in ('SceneColor', 'Albedo') else 2
    if meta.get('resource') != 'PathTracing.'+name or meta.get('format') != expected_format:
        raise ValueError('Wrong capture identity/format')
    if (meta.get('nrdPresentation') != RELAX or meta.get('nrdFailure') != '' or
            meta.get('nrdLastEvaluation', {}).get('recorded') is not True):
        raise ValueError('NRD was not successfully recorded and presented')
    comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    if common is not None and comparable != common:
        raise ValueError('Mismatched stable-radiance capture cohort')
    return comparable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == FIXTURE or FIXTURE in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    output.mkdir(parents=True, exist_ok=True)
    for name in ('scene.json', 'render-preset.json', 'panels.gltf'):
        shutil.copyfile(FIXTURE/name, output/name)
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    report = dict(status='running', fullIntegrationReady=False, records=[], failures=[],
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)),
        fixtureSha256={name: sha(output/name) for name in ('scene.json', 'render-preset.json', 'panels.gltf')},
        shaderSha256={name: sha(ROOT/'build/shaders'/name) for name in
            ('shaders_PathTracing_CSMain.cso', 'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso')})
    write_json(output/'report.json', report)
    try:
        buffers, common = {}, None
        for name in RESOURCES:
            (meta, image), record = capture(exe, output, name, output/'render-preset.json', name)
            report['records'].append(record)
            common = check_metadata(meta, name, common)
            buffers[name] = image
            write_json(output/'report.json', report)
            print('captured '+name, flush=True)
        report.update(status='passed', assessment=assess_stable(buffers), metadata=common)
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'Fixed opaque fixture, raw per-pixel input/composition contract; not temporal or subjective quality acceptance.'
    write_json(output/'report.json', report)
    print(report['status'], report.get('assessment'), report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
