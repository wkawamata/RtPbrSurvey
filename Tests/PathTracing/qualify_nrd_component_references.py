"""Acquire paired one-spp component references with replay and fixed ROI qualification."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import load_references
from validate_nrd_reference_components import COMPONENTS, closure
from validate_nrd_glossy_seeds import check_capture
from validate_nrd_prepass_bridge import check_prepass
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_rois import ROOT, EXPECTED
from validate_part1 import sha, write_json

SEEDS = (11, 29)
SAMPLES = 8192
SPP = 1


def qualify(first, second, mask, component, roi):
    if component not in ('total', 'diffuse', 'specular') or roi not in EXPECTED:
        raise ValueError('Unexpected component/ROI identity')
    if (first.shape != second.shape or len(first.shape) != 3 or first.shape[-1] != 4 or
            mask.shape != first.shape[:2] or mask.dtype != bool or int(mask.sum()) < 100 or
            not np.isfinite(first).all() or not np.isfinite(second).all() or (first < 0).any() or (second < 0).any()):
        raise ValueError('Invalid reference pair/ROI')
    a, b = first[mask, :3].astype(float), second[mask, :3].astype(float)
    mean = (a+b)*.5
    rms = float(np.sqrt(np.mean(mean**2)))
    row = dict(component=component, roi=roi, pixels=int(mask.sum()), referenceMeanRgb=mean.mean(axis=0).tolist(),
        referenceRms=rms, maximumPairAbsoluteDifference=float(abs(a-b).max()))
    if component == 'diffuse' and roi.startswith('metal-'):
        if np.count_nonzero(a) or np.count_nonzero(b):
            raise ValueError('Pure-metal diffuse reference unexpectedly contains energy')
        return dict(row, status='exact-zero-inactive', qualified=True, referencePairNrmse=None)
    if rms <= 1e-6:
        return dict(row, status='inconclusive-dark', qualified=False, referencePairNrmse=None)
    pair = float(np.sqrt(np.mean((a-b)**2))/rms)
    passed = pair <= .03
    return dict(row, status='qualified-pair' if passed else 'inconclusive-reference', qualified=passed,
        referencePairNrmse=pair)


def grouping(first, second):
    if first.shape != second.shape or not np.isfinite([first, second]).all():
        raise ValueError('Invalid grouped-sampling images')
    a, b = first[..., :3].astype(float), second[..., :3].astype(float)
    bound = abs(np.spacing(first[..., :3].astype(np.float16)).astype(float))
    bound += abs(np.spacing(second[..., :3].astype(np.float16)).astype(float))
    bound += 64*(256+4)*np.finfo(np.float32).eps*np.maximum(np.maximum(a, b), 1e-8)
    error = abs(a-b)
    if not (error <= bound).all():
        raise ValueError('Grouping 16 samples changed reference transport beyond the fixed numerical budget')
    return dict(maximumAbsoluteDifference=float(error.max()), maximumBoundRatio=float((error/bound).max()))


def capture(exe, folder, name, samples, seed, target):
    label = name
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    cmd = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'render-preset.json'),
        '-EnablePathTracing']
    if name != 'total':
        cmd += ['-PathTracingReferenceComponent', name]
    cmd += ['-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path), '-PathTracingSamples',
        str(samples), '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    print('starting', seed, name, samples, flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True, timeout=900)
    record = dict(label=label, component=name, seed=seed, samples=samples, command=cmd, evidenceFolder=str(folder),
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
    meta, image, runtime = read_evidence(folder, label, record)
    record.update(metadata=meta, diagnostics=runtime)
    if (meta.get('resource') != 'PathTracing.SceneColor' or meta.get('format') != 10 or
            meta.get('sampleStartIndex') != samples or meta.get('samplesPerFrame') != SPP or
            meta.get('randomSeed') != seed or meta.get('primarySampling') != 'pixel-center' or
            meta.get('nrdPreviewRequested') is not False or meta.get('nrdPresentation') != 'Native PT' or
            meta.get('denoiserDistanceDiagnostics') is not False or
            meta.get('pathTracingReferenceComponent') != (None if name == 'total' else name)):
        raise ValueError('Wrong grouped reference capture identity')
    expected = dict(accumulatedSamples=samples, samplesPerFrame=SPP, randomSeed=seed, targetSamples=samples,
        maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
        emissiveEnabled=False, russianRoulette=False, nrdPreviewRequested=False, nrdFailureCount=0)
    if any(runtime.get(key) != value for key, value in expected.items()):
        raise ValueError('Wrong grouped reference transport state')
    for key in ('width', 'height', 'viewProjection', 'previousViewProjection', 'inverseViewProjection',
            'rayTMin', 'rayTMax', 'normalBias', 'lensShift'):
        if meta.get(key) != target[key]:
            raise ValueError('Grouped reference camera/geometry changed: '+key)
    return image, record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plumbing', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plumbing, source, exe = (p.resolve(strict=True) for p in (args.plumbing, args.source, args.executable))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (plumbing, source, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be new/separate')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {str(p): sha(p) for p in (exe, Path(__file__), plumbing/'report.json', source/'report.json',
        Path(__file__).with_name('validate_nrd_reference_components.py'),
        exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso')}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, samples=SAMPLES, seeds=list(SEEDS),
        samplesPerFrame=SPP, pairNrmseLimit=.03, masksReselected=False, records=[], grouping={}, rois=[],
        failures=[], fullIntegrationReady=False, limitation='Reference variance qualification only; no NRD quality verdict.')
    write_json(output/'report.json', report)
    try:
        scene, preset, previous, target, regions, _, _ = load_references(source)
        old = json.loads((plumbing/'report.json').read_text(encoding='utf-8'))
        if old.get('status') != 'passed-reference-component-plumbing' or old.get('failures') != [] or len(old['records']) != 8:
            raise ValueError('Missing successful component plumbing validation')
        for p, digest in old['artifactSha256'].items():
            if sha(Path(p)) != digest:
                raise ValueError('Plumbing artifact changed')
        for p, digest in old['fixtureSha256'].items():
            if sha(Path(p)) != digest:
                raise ValueError('Plumbing fixture changed')
        if old['artifactSha256'].get(str(exe)) != sha(exe):
            raise ValueError('Reference executable differs from validated plumbing')
        keys = [row['label'] for row in old['records']]
        if len(set(keys)) != 8 or set(keys) != set(COMPONENTS) | {'total', 'bridge-SceneColor', 'bridge-NrdSceneColor', 'guarded-diffuse'}:
            raise ValueError('Changed plumbing inventory')
        one = {}
        for record in old['records']:
            label = record['label']
            folder = plumbing if label.startswith('bridge-') else plumbing/'guard' if label == 'guarded-diffuse' else plumbing/'references'
            resource = label.removeprefix('bridge-') if label.startswith('bridge-') else 'NrdValidity' if label == 'guarded-diffuse' else 'SceneColor'
            component = 'diffuse' if label == 'guarded-diffuse' else label if label in COMPONENTS else None
            expected_command = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset',
                str(folder/'render-preset.json'), '-EnablePathTracing']
            if component:
                expected_command += ['-PathTracingReferenceComponent', component]
            expected_command += ['-DebugPreviewResource', 'PathTracing.'+resource, '-CapturePath', str(folder/(label+'.ptbuf'))]
            expected_command += ['-CaptureAfterFrames', '30'] if label.startswith('bridge-') or label == 'guarded-diffuse' else ['-PathTracingSamples', '256']
            expected_command += ['-LogToFile', str(folder/(label+'.log')), '-ExitAfterCapture', '-DisableVSync']
            if record.get('command') != expected_command or record.get('resource') != resource or record.get('component') != component:
                raise ValueError('Plumbing command/resource identity changed')
            meta, image, runtime = read_evidence(folder, label, record)
            if meta != record['metadata']:
                raise ValueError('Plumbing metadata changed')
            if label in (*COMPONENTS, 'total'):
                if (meta.get('pathTracingReferenceComponent') != component or meta.get('sampleStartIndex') != 256 or
                        meta.get('samplesPerFrame') != 1 or meta.get('randomSeed') != 11 or
                        meta.get('nrdPreviewRequested') is not False or runtime.get('accumulatedSamples') != 256):
                    raise ValueError('Plumbing reference sampling changed')
                one[label] = image
            elif label.startswith('bridge-'):
                name = record['resource']
                check_capture(meta, runtime, name, 7, 30, target, None)
                check_prepass(meta)
                if not np.array_equal(image, previous[name]):
                    raise ValueError('Plumbing normal path no longer matches historical control')
            elif np.count_nonzero(image) or not meta.get('nrdPresentation', '').startswith('Native fallback:'):
                raise ValueError('Plumbing diagnostic admission guard changed')
        if closure(one) != old['closure']:
            raise ValueError('Plumbing energy closure changed')
        fixtures = {}
        def prepare(folder, seed):
            changed = copy.deepcopy(preset)
            changed['pathTracing'].update(samplesPerFrame=SPP, nrdPreview=False, denoiserDistanceDiagnostics=False,
                randomSeed=seed, debugOutput=3)
            write_json(folder/'scene.json', scene)
            write_json(folder/'render-preset.json', changed)
            for name in ('scene.json', 'render-preset.json'):
                fixtures[str(folder/name)] = sha(folder/name)
        folder = output/'replay'
        prepare(folder, 11)
        grouped = {}
        for name in ('total', *COMPONENTS):
            image, record = capture(exe, folder, name, 256, 11, target)
            grouped[name] = image
            report['records'].append(record)
            if not np.array_equal(one[name], image):
                raise ValueError('One-spp replay differs from validated component plumbing: '+name)
            report['grouping'][name] = dict(bitIdentical=True, samplesPerFrame=SPP)
            write_json(output/'report.json', report)
        report['groupingClosure'] = closure(grouped)
        references = {}
        for seed in SEEDS:
            folder = output/str(seed)
            prepare(folder, seed)
            for name in ('total', 'diffuse', 'specular'):
                image, record = capture(exe, folder, name, SAMPLES, seed, target)
                references[seed, name] = image
                report['records'].append(record)
                write_json(output/'report.json', report)
        for name in ('total', 'diffuse', 'specular'):
            for roi in sorted(EXPECTED):
                report['rois'].append(qualify(references[11, name], references[29, name], regions[roi], name, roi))
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()) or any(sha(Path(p)) != digest for p, digest in fixtures.items()):
            raise ValueError('Acquisition artifacts/fixtures changed')
        report.update(status='completed-reference-pair-study', fixtureSha256=fixtures,
            referenceVerdict='qualified-all-components' if all(row['qualified'] for row in report['rois']) else 'partially-inconclusive')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report.get('referenceVerdict'), report['failures'], flush=True)
    return report['status'] != 'completed-reference-pair-study'


if __name__ == '__main__':
    raise SystemExit(main())
