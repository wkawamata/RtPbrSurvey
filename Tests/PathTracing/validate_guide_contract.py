"""Validate native PT guide ranges, miss sentinels and frame signal partition."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

from validate_inputs import read_buffer, plane_hits
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('NormalRoughness', 'ViewZ', 'MotionVectors', 'Albedo',
             'DiffuseRadianceHitT', 'SpecularRadianceHitT', 'Accumulation')
FORMATS = (10, 41, 34, 10, 10, 10, 2)


def assess(buffers, samples_per_frame=1, require_miss=True):
    if any(not np.isfinite(buffers[name][1]).all() for name in RESOURCES):
        raise ValueError('Nonfinite buffer values')
    metadata = [buffers[name][0] for name in RESOURCES]
    reference = metadata[0]
    for name, meta, fmt in zip(RESOURCES, metadata, FORMATS):
        if meta['resource'] != 'PathTracing.' + name or meta['format'] != fmt:
            raise ValueError('Unexpected resource/format: ' + name)
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex',
                    'viewProjection', 'previousViewProjection'):
            if meta[key] != reference[key]:
                raise ValueError('Capture frame mismatch: ' + key)
    normal, depth, motion, albedo, diffuse, specular, accumulation = (
        buffers[name][1] for name in RESOURCES)
    hit = albedo[..., 3] == 1
    miss = albedo[..., 3] == 0
    if not hit.any() or (require_miss and not miss.any()) or not (hit | miss).all():
        raise ValueError('Fixture must contain hit and miss pixels with binary validity')
    if not require_miss and miss.any():
        raise ValueError('Marker batch fixture requires all primary samples to hit')
    checks = dict(
        normalUnit=bool((abs(np.linalg.norm(normal[hit, :3], axis=1)-1) <= .002).all()),
        roughnessRange=bool(((normal[..., 3] >= 0) & (normal[..., 3] <= 1)).all()),
        positiveHitDepth=bool((depth[hit, 0] > 0).all()),
        nonnegativeAlbedo=bool((albedo[..., :3] >= 0).all()),
        nonnegativeSignals=bool((diffuse[..., :3] >= 0).all() and (specular[..., :3] >= 0).all()),
        nonzeroDiffuse=bool((diffuse[hit, :3] > 0).any()),
        nonzeroSpecular=bool((specular[hit, :3] > 0).any()),
        hitDistanceAgreement=bool(np.array_equal(diffuse[..., 3], specular[..., 3])),
        positiveHitDistance=bool((diffuse[hit, 3] > 0).all()),
        missNormalRoughness=bool((normal[miss] == [0, 0, 0, 1]).all()),
        missDepth=bool((depth[miss] == 0).all()),
        missMotion=bool((motion[miss] == 0).all()),
        missAlbedo=bool((albedo[miss] == 0).all()),
        missSignals=bool((diffuse[miss] == 0).all() and (specular[miss] == 0).all()),
        singleFrameAccumulation=bool((accumulation[..., 3] == samples_per_frame).all()))
    frame_radiance = accumulation[hit, :3] / samples_per_frame
    error = abs(diffuse[hit, :3] + specular[hit, :3] - frame_radiance)
    bound = .002 * np.maximum(abs(frame_radiance), 1) + .00001
    checks['signalPartition'] = bool((error <= bound).all())
    return dict(passed=all(checks.values()), checks=checks,
                hitPixels=int(hit.sum()), missPixels=int(miss.sum()),
                signalPartitionMaxError=float(error.max()),
                signalPartitionBound='0.002 * max(abs(frame radiance), 1) + 1e-5',
                samplesPerFrame=samples_per_frame,
                limitation='Static fixture, emission disabled. Consistency is not independent estimator validation.')


def compare_primary_guides(reference, current):
    for name in RESOURCES[:4]:
        for key in ('width', 'height', 'randomSeed', 'sampleStartIndex',
                    'viewProjection', 'previousViewProjection'):
            if reference[name][0][key] != current[name][0][key]:
                raise ValueError('Primary sample metadata differs: ' + key)
        if not np.array_equal(reference[name][1], current[name][1]):
            raise ValueError('First-sample guide changed across batches: ' + name)


def run_batch_matrix(exe, output, analyze_only=False):
    report = dict(schemaVersion=1, status='running', exeSha256=sha(exe), records=[], failures=[])
    report['analysisScriptSha256'] = sha(Path(__file__).resolve())
    report['boundaryRoi'] = 'Interior pixels excluding 16-pixel border, stride 1'
    reference = None
    for batch in (1, 2, 4):
        directory = output/str(batch)
        command = [sys.executable, '-B', str(Path(__file__).resolve()),
                   '--exe', str(exe), '--output', str(directory), '--fixture', 'input-marker',
                   '--samples-per-frame', str(batch), '--capture-after-frames', str(60//batch)]
        try:
            if not analyze_only:
                subprocess.run(command, check=True)
            child_report = json.loads((directory/'report.json').read_text())
            if child_report['status'] != 'passed' or child_report['exeSha256'] != report['exeSha256']:
                raise ValueError('Invalid child cohort or executable mismatch')
            buffers = {name: read_buffer(directory/(name+'.ptbuf')) for name in RESOURCES[:4]}
            if reference is None:
                reference = buffers
            else:
                compare_primary_guides(reference, buffers)
            meta = buffers['Albedo'][0]
            xs, ys = np.meshgrid(np.arange(16, meta['width']-16),
                                 np.arange(16, meta['height']-16))
            materials = []
            for offset in range(batch):
                sample_meta = dict(meta, sampleStartIndex=meta['sampleStartIndex']+offset)
                materials.append(plane_hits(sample_meta, xs.ravel(), ys.ravel(), True)[1])
            mixed = np.any(np.stack(materials) != materials[0], axis=0)
            if batch > 1 and not mixed.any():
                raise ValueError('No mixed-material boundary samples; test is inconclusive')
            report['records'].append(dict(samplesPerFrame=batch, sampleStartIndex=meta['sampleStartIndex'],
                primaryGuidesIdentical=True, sampledPixels=int(mixed.size),
                mixedMaterialPixels=int(mixed.sum()), childReportSha256=sha(directory/'report.json')))
        except Exception as error:
            report['failures'].append(dict(samplesPerFrame=batch, error=str(error)))
        write_json(output/'report.json', report)
    report['status'] = 'passed' if not report['failures'] else 'failed'
    report['limitation'] = 'All-hit marker fixture: no mixed hit/miss test, no backend demodulation or object animation.'
    write_json(output/'report.json', report)
    print(json.dumps(report), flush=True)
    return bool(report['failures'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', choices=('roughness', 'input-marker'), default='roughness')
    parser.add_argument('--samples-per-frame', type=int, choices=(1, 2, 4), default=1)
    parser.add_argument('--capture-after-frames', type=int, default=30)
    parser.add_argument('--batch-matrix', action='store_true')
    parser.add_argument('--analyze-batch-matrix', action='store_true')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()) and not args.analyze_batch_matrix:
        parser.error('Use an empty output directory')
    output.mkdir(parents=True, exist_ok=True)
    if args.capture_after_frames < 1:
        parser.error('Capture frame must be positive')
    if args.batch_matrix or args.analyze_batch_matrix:
        if args.analyze_batch_matrix and (output/'report.json').exists() and not (output/'report-before-dense-analysis.json').exists():
            write_json(output/'report-before-dense-analysis.json',
                       json.loads((output/'report.json').read_text()))
        return run_batch_matrix(exe, output, args.analyze_batch_matrix)
    if args.fixture == 'roughness' and args.samples_per_frame != 1:
        parser.error('Mixed hit/miss roughness contract currently requires one sample/frame')
    directory = ROOT/'Assets/Scenes/PathTracingValidation'/args.fixture
    scene = json.loads((directory/'scene.json').read_text())
    preset = json.loads((directory/'render-preset.json').read_text())
    preset['pathTracing'].update(accumulate=False, samplesPerFrame=args.samples_per_frame, randomSeed=7,
                                emissiveEnabled=False, debugOutput=3)
    if args.fixture == 'input-marker':
        preset['pathTracing']['maxBounces'] = 2
    scene['renderPreset'] = 'preset.json'
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    report = dict(schemaVersion=1, status='running', executable=str(exe),
                  exeSha256=sha(exe), workspace=str(ROOT),
                  baseCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  sceneSha256=sha(output/'scene.json'), presetSha256=sha(output/'preset.json'),
                  shaderSha256=sha(ROOT/'Shaders/shaders_PathTracing.hlsl'), records=[], failures=[])
    buffers = {}
    for name in RESOURCES:
        path, log = output/(name+'.ptbuf'), output/(name+'.log')
        command = [str(exe), '-SceneFile', str(output/'scene.json'),
                   '-RenderPreset', str(output/'preset.json'), '-EnablePathTracing',
                   '-PathTracingSeed', '7', '-DebugPreviewResource', 'PathTracing.'+name,
                   '-CapturePath', str(path), '-CaptureAfterFrames', str(args.capture_after_frames),
                   '-LogToFile', str(log), '-ExitAfterCapture']
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
            buffers[name] = read_buffer(path)
            record.update(sha256=sha(path), metadata=buffers[name][0], d3d12Errors=0)
            print('Captured ' + name, flush=True)
        except Exception as error:
            report['failures'].append(dict(resource=name, error=str(error)))
        write_json(output/'report.json', report)
    if not report['failures']:
        try:
            report['assessment'] = assess(buffers, args.samples_per_frame, args.fixture == 'roughness')
            if not report['assessment']['passed']:
                report['failures'].append(dict(stage='assessment', error='Guide contract checks failed'))
        except Exception as error:
            report['failures'].append(dict(stage='assessment', error=str(error)))
    report['status'] = 'passed' if not report['failures'] else 'failed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
