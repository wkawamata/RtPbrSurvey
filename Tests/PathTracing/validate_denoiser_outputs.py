"""Validate current-frame PT signals without enabling a denoiser."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_guide_contract import RESOURCES as LEGACY_RESOURCES
from validate_materials import make_fixture

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('Albedo', 'DiffuseRadianceHitT', 'SpecularRadianceHitT',
             'Accumulation', 'CurrentFrameRadiance', 'MaterialF0Metallic',
             'StableRadiance', 'BackgroundRadiance')
FORMATS = (10, 10, 10, 2, 2, 10, 2, 2)


def assess(buffers, stable_fixture=False):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected resources')
    reference = buffers['Albedo'][0]
    for name, fmt in zip(RESOURCES, FORMATS):
        meta, values = buffers[name]
        if meta['resource'] != 'PathTracing.' + name or meta['format'] != fmt:
            raise ValueError('Unexpected resource/format: ' + name)
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex',
                    'viewProjection', 'previousViewProjection'):
            if meta[key] != reference[key]:
                raise ValueError('Capture frame mismatch: ' + key)
        if values.shape != (meta['height'], meta['width'], 4) or not np.isfinite(values).all():
            raise ValueError('Invalid shape or nonfinite values: ' + name)
    albedo, diffuse, specular, accumulation, current, material, stable, background = (
        buffers[name][1] for name in RESOURCES)
    hit = albedo[..., 3] == 1
    miss = albedo[..., 3] == 0
    if not hit.any() or not miss.any() or not (hit | miss).all():
        raise ValueError('Fixture requires hit and miss pixels')
    filtered_diffuse = diffuse[..., :3] - stable[..., :3]
    reconstructed = filtered_diffuse + specular[..., :3] + stable[..., :3] + background[..., :3]
    bound = .002 * np.maximum(abs(current[..., :3]), 1) + 1e-5
    error = abs(reconstructed - current[..., :3])
    expected_f0 = .04 * (1 - material[..., 3:4]) + albedo[..., :3] * material[..., 3:4]
    checks = dict(
        reconstruction=bool((error <= bound).all()),
        currentNotAccumulated=bool(np.array_equal(current[..., :3], accumulation[..., :3])
                                  and (accumulation[..., 3] == 1).all()),
        currentAlpha=bool((current[..., 3] == 1).all()),
        materialRange=bool(((material >= 0) & (material <= 1)).all()),
        materialF0=bool((abs(material[hit, :3] - expected_f0[hit]) <= .0015).all()),
        missMaterial=bool((material[miss] == 0).all()),
        stableMiss=bool((stable[miss] == 0).all()),
        backgroundHit=bool((background[hit] == 0).all()),
        backgroundMiss=bool(np.array_equal(background[miss, :3], current[miss, :3])),
        signalAlpha=bool((stable[..., 3] == 0).all() and (background[..., 3] == 0).all()),
        nonnegative=bool((current >= 0).all() and (stable >= 0).all() and (background >= 0).all()),
        nonnegativeFilteredDiffuse=bool((filtered_diffuse >= -bound).all()))
    if stable_fixture:
        left = hit.copy()
        left[:, left.shape[1] // 2:] = False
        right = hit & ~left
        if min(left.sum(), right.sum()) < 100:
            raise ValueError('Insufficient visible emitter pixels')
        checks['primaryEmission'] = bool((abs(stable[left, :3] - [2, .5, .25]) < .0001).all())
        checks['secondPrimaryEmission'] = bool((abs(stable[right, :3] - [.1, .2, .3]) < .0001).all())
    else:
        checks['noPrimaryEmission'] = bool((stable == 0).all())
    return dict(passed=all(checks.values()), checks=checks,
                reconstructionMaxError=float(error.max()), hitPixels=int(hit.sum()), missPixels=int(miss.sum()),
                limitation='One path/pixel, static radiance mode; no NRD packing, dispatch or history validation.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--stable-fixture', action='store_true')
    parser.add_argument('--progressive-reference', type=Path)
    args = parser.parse_args()
    exe, output = args.exe.resolve(strict=True), args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    if args.stable_fixture and args.baseline:
        parser.error('Baseline comparison requires the unchanged roughness fixture')
    if args.progressive_reference and (args.stable_fixture or args.baseline):
        parser.error('Progressive check requires only a matching current-frame reference')
    output.mkdir(parents=True, exist_ok=True)
    if args.stable_fixture:
        scene_path, preset_path, asset = make_fixture(output)
        gltf = json.loads(asset.read_text())
        for index, material in enumerate(gltf['materials']):
            material.pop('normalTexture', None)
            material.pop('emissiveTexture', None)
            material['emissiveFactor'] = [2, .5, .25] if index == 0 else [.1, .2, .3]
        write_json(asset, gltf)
        preset = json.loads(preset_path.read_text())
    else:
        directory = ROOT/'Assets/Scenes/PathTracingValidation/roughness'
        scene = json.loads((directory/'scene.json').read_text())
        scene['renderPreset'] = 'preset.json'
        scene_path, preset_path = output/'scene.json', output/'preset.json'
        write_json(scene_path, scene)
        preset = json.loads((directory/'render-preset.json').read_text())
    preset['pathTracing'].update(accumulate=bool(args.progressive_reference), samplesPerFrame=1, randomSeed=7,
                                emissiveEnabled=args.stable_fixture, debugOutput=3)
    if args.stable_fixture:
        preset['lighting']['emissiveEnabled'] = True
    write_json(preset_path, preset)
    report = dict(schemaVersion=1, status='running', exeSha256=sha(exe),
                  sceneSha256=sha(scene_path), presetSha256=sha(preset_path),
                  shaderSha256=sha(ROOT/'Shaders/shaders_PathTracing.hlsl'),
                  analysisScriptSha256=sha(Path(__file__)), records=[], failures=[])
    buffers = {}
    names = tuple(dict.fromkeys((*RESOURCES, *(LEGACY_RESOURCES if args.baseline else ()))))
    if args.progressive_reference:
        names = ('CurrentFrameRadiance', 'Accumulation')
    for name in names:
        path, log = output/(name+'.ptbuf'), output/(name+'.log')
        command = [str(exe), '-SceneFile', str(scene_path), '-RenderPreset', str(preset_path),
                   '-EnablePathTracing', '-PathTracingSeed', '7', '-DebugPreviewResource', 'PathTracing.'+name,
                   '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
        record = dict(resource=name, command=command)
        report['records'].append(record)
        try:
            child = subprocess.Popen(command, cwd=ROOT)
            try:
                if child.wait(timeout=180) != 0:
                    raise RuntimeError('Application failed')
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            text = log.read_text(encoding='utf-8-sig')
            if '[ERROR]' in text or '[CORRUPTION]' in text:
                raise RuntimeError('D3D12 errors in capture log')
            meta, values = read_buffer(path)
            record.update(sha256=sha(path), d3d12Errors=0)
            if args.baseline and name in LEGACY_RESOURCES:
                old_meta, old_values = read_buffer(args.baseline/(name+'.ptbuf'))
                if meta != old_meta or not np.array_equal(values, old_values):
                    raise ValueError('Native baseline changed: ' + name)
                record['nativeBaselineIdentical'] = True
            if name in RESOURCES:
                buffers[name] = (meta, values)
            print('Captured ' + name, flush=True)
        except Exception as error:
            report['failures'].append(dict(resource=name, error=str(error)))
        write_json(output/'report.json', report)
    if not report['failures']:
        try:
            if args.progressive_reference:
                report['assessment'] = assess_progressive(buffers, read_buffer(
                    args.progressive_reference/'CurrentFrameRadiance.ptbuf'))
            else:
                report['assessment'] = assess(buffers, args.stable_fixture)
            if not report['assessment']['passed']:
                report['failures'].append(dict(stage='assessment', error='Output contract failed'))
        except Exception as error:
            report['failures'].append(dict(stage='assessment', error=str(error)))
    report['status'] = 'passed' if not report['failures'] else 'failed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


def assess_progressive(buffers, reference):
    if set(buffers) != {'CurrentFrameRadiance', 'Accumulation'}:
        raise ValueError('Missing progressive resources')
    current_meta, current = buffers['CurrentFrameRadiance']
    accum_meta, accumulation = buffers['Accumulation']
    reference_meta, reference_values = reference
    for name, meta in [('CurrentFrameRadiance', current_meta), ('Accumulation', accum_meta)]:
        if meta['resource'] != 'PathTracing.' + name or meta['format'] != 2:
            raise ValueError('Unexpected progressive resource/format')
        for key in ('width', 'height', 'sampleStartIndex', 'randomSeed',
                    'viewProjection', 'previousViewProjection'):
            if meta[key] != reference_meta[key]:
                raise ValueError('Progressive cohort mismatch: ' + key)
    if current.shape != reference_values.shape or accumulation.shape != current.shape:
        raise ValueError('Progressive dimensions mismatch')
    if not np.isfinite(current).all() or not np.isfinite(accumulation).all():
        raise ValueError('Nonfinite progressive values')
    checks = dict(currentIndependentOfHistory=bool(np.array_equal(current, reference_values)),
                  historyAccumulated=bool((accumulation[..., 3] > 1).all()),
                  historyDiffersFromCurrent=bool(not np.array_equal(
                      accumulation[..., :3] / accumulation[..., 3:4], current[..., :3])))
    return dict(passed=all(checks.values()), checks=checks)


if __name__ == '__main__':
    raise SystemExit(main())
