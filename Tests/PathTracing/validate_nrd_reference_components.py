"""Validate diagnostic accumulated physical components before acquiring converged references."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import load_references
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_seeds import check_capture
from validate_nrd_prepass_bridge import check_prepass
from validate_nrd_glossy_rois import ROOT
from validate_part1 import sha, write_json

COMPONENTS = ('diffuse', 'specular', 'stable', 'background')
SAMPLES = 256


def closure(buffers):
    if set(buffers) != set(COMPONENTS) | {'total'}:
        raise ValueError('Incomplete accumulated-component cohort')
    shape = buffers['total'].shape
    if len(shape) != 3 or shape[-1] != 4 or any(image.shape != shape or
            not np.isfinite(image).all() or (image < 0).any() or (image[..., 3] != 1).any()
            for image in buffers.values()):
        raise ValueError('Malformed accumulated-component image')
    total = buffers['total'][..., :3].astype(float)
    summed = sum(buffers[name][..., :3].astype(float) for name in COMPONENTS)
    # FP16 capture quantization plus a conservative positive FP32 accumulation budget.
    bound = sum(np.abs(np.spacing(buffers[name][..., :3].astype(np.float16)).astype(float))
        for name in (*COMPONENTS, 'total'))
    bound += 64*(SAMPLES+4)*np.finfo(np.float32).eps*np.maximum(np.maximum(total, summed), 1e-8)
    error = abs(summed-total)
    if not (error <= bound).all():
        raise ValueError('Accumulated physical components do not reconstruct total radiance')
    return dict(maximumAbsoluteError=float(error.max()), maximumBoundRatio=float((error/bound).max()),
        pixels=int(total.shape[0]*total.shape[1]), samples=SAMPLES,
        bound='Sum of 1 FP16 ULP per captured term + 64*(256+4)*eps32*max(total,sum,1e-8)',
        convergedReferenceQualified=False)


def capture(exe, folder, label, resource, component=None, samples=None, frames=30):
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    command = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'render-preset.json'),
        '-EnablePathTracing']
    if component:
        command += ['-PathTracingReferenceComponent', component]
    command += ['-DebugPreviewResource', 'PathTracing.'+resource, '-CapturePath', str(path)]
    command += ['-PathTracingSamples', str(samples)] if samples else ['-CaptureAfterFrames', str(frames)]
    command += ['-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    subprocess.run(command, cwd=ROOT, check=True, timeout=180)
    record = dict(label=label, resource=resource, command=command, component=component,
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
    meta, image, runtime = read_evidence(folder, label, record)
    record.update(metadata=meta, diagnostics=runtime)
    return image, record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, exe = args.source.resolve(strict=True), args.executable.resolve(strict=True)
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (source, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be new/separate')
    output.mkdir(parents=True, exist_ok=True)
    paths = [exe, Path(__file__), source/'report.json', exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso']
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated', artifactSha256=artifacts,
        records=[], failures=[], fullIntegrationReady=False,
        limitation='256-sample energy closure and diagnostic admission only, not converged per-lobe reference quality.')
    write_json(output/'report.json', report)
    try:
        scene, preset, previous, target, _, _, _ = load_references(source)
        write_json(output/'scene.json', scene)
        write_json(output/'render-preset.json', preset)
        fixture_hashes = {}
        for name in ('scene.json', 'render-preset.json'):
            fixture_hashes[str(output/name)] = sha(output/name)
        for name in ('SceneColor', 'NrdSceneColor'):
            image, record = capture(exe, output, 'bridge-'+name, name)
            report['records'].append(record)
            check_capture(record['metadata'], record['diagnostics'], name, 7, 30, target, None)
            check_prepass(record['metadata'])
            if 'pathTracingReferenceComponent' in record['metadata'] or not np.array_equal(image, previous[name]):
                raise ValueError('Normal Native/NRD path differs from historical control')
            write_json(output/'report.json', report)
        guard = output/'guard'
        write_json(guard/'scene.json', scene)
        write_json(guard/'render-preset.json', preset)
        validity, record = capture(exe, guard, 'guarded-diffuse', 'NrdValidity', 'diffuse')
        report['records'].append(record)
        meta, runtime = record['metadata'], record['diagnostics']
        if (np.count_nonzero(validity) or meta.get('pathTracingReferenceComponent') != 'diffuse' or
                meta.get('nrdPreviewRequested') is not True or not meta.get('nrdPresentation', '').startswith('Native fallback:') or
                meta.get('sampleStartIndex') != 30 or runtime.get('accumulatedSamples') != 30):
            raise ValueError('Reference component was incorrectly admitted as NRD radiance')
        folder = output/'references'
        changed = copy.deepcopy(preset)
        changed['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False, samplesPerFrame=1,
            randomSeed=11, debugOutput=3)
        write_json(folder/'scene.json', scene)
        write_json(folder/'render-preset.json', changed)
        buffers, common = {}, None
        for name in ('total', *COMPONENTS):
            image, record = capture(exe, folder, name, 'SceneColor', None if name == 'total' else name, samples=SAMPLES)
            report['records'].append(record)
            meta, runtime = record['metadata'], record['diagnostics']
            if (meta.get('resource') != 'PathTracing.SceneColor' or meta.get('format') != 10 or
                    meta.get('samplesPerFrame') != 1 or meta.get('sampleStartIndex') != SAMPLES or
                    meta.get('randomSeed') != 11 or meta.get('primarySampling') != 'pixel-center' or
                    meta.get('nrdPreviewRequested') is not False or meta.get('nrdPresentation') != 'Native PT' or
                    meta.get('pathTracingReferenceComponent') != (None if name == 'total' else name) or
                    runtime.get('accumulatedSamples') != SAMPLES or runtime.get('targetSamples') != SAMPLES or
                    runtime.get('maxBounces') != 4 or runtime.get('environmentSamplingMode') != 7):
                raise ValueError('Wrong component-reference capture state')
            for key in ('width', 'height', 'viewProjection', 'inverseViewProjection', 'lensShift', 'normalBias'):
                if meta.get(key) != target[key]:
                    raise ValueError('Component-reference camera/ray settings changed')
            comparable = {key: meta[key] for key in ('sampleStartIndex', 'randomSeed', 'samplesPerFrame',
                'viewProjection', 'previousViewProjection', 'inverseViewProjection', 'rayTMin', 'rayTMax', 'normalBias')}
            if common is not None and comparable != common:
                raise ValueError('Component-reference cohort mismatch')
            common = comparable
            buffers[name] = image
            write_json(output/'report.json', report)
            print('captured', name, SAMPLES, flush=True)
        report['closure'] = closure(buffers)
        for folder_path in (guard, folder):
            for name in ('scene.json', 'render-preset.json'):
                fixture_hashes[str(folder_path/name)] = sha(folder_path/name)
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Reference-acquisition artifacts changed')
        report.update(status='passed-reference-component-plumbing', fixtureSha256=fixture_hashes)
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-reference-component-plumbing'


if __name__ == '__main__':
    raise SystemExit(main())
