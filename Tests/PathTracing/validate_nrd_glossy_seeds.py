"""Measure fixed glossy ROIs across predeclared seeds and warm-up checkpoints."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import load_references, ROI
from refine_nrd_glossy_rois import identities
from requalify_nrd_glossy import read_evidence
from validate_nrd_camera_motion import assess_frame
from validate_nrd_glossy_rois import ROOT, FIXTURE, EXPECTED, ROI_LIMITS, SELECTION, assess_roi, roi_verdict
from validate_part1 import sha, write_json

SEEDS = (7, 101, 307, 701, 1009)
FRAMES = (30, 120)
RESOURCES = ('SceneColor', 'NrdSceneColor', 'NrdCompositionDiagnostics')
LUMINANCE = np.array([.2126, .7152, .0722])


def check_capture(meta, runtime, resource, seed, frames, target, common):
    if (seed not in SEEDS or frames not in FRAMES or resource not in RESOURCES or meta.get('resource') != 'PathTracing.'+resource or
            meta.get('format') != (10 if resource == 'SceneColor' else 2) or
            meta.get('sampleStartIndex') != frames or meta.get('randomSeed') != seed or
            meta.get('samplesPerFrame') != 1 or meta.get('primarySampling') != 'pixel-center' or
            meta.get('denoiserDistanceDiagnostics') is not True or meta.get('nrdPreviewRequested') is not True or
            meta.get('nrdPresentation') != 'NRD RELAX diagnostic (per-pixel Native fallback)' or meta.get('nrdFailure') != ''):
        raise ValueError('Wrong seed-study capture identity/sampling')
    expected = dict(accumulatedSamples=frames, targetSamples=0, samplesPerFrame=1, randomSeed=seed,
        maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
        emissiveEnabled=False, russianRoulette=False, denoiserDistanceDiagnostics=True, nrdFailureCount=0)
    if any(runtime.get(key) != value for key, value in expected.items()):
        raise ValueError('Wrong seed-study runtime transport')
    for key in ('width', 'height', 'rayTMin', 'rayTMax', 'normalBias', 'lensShift',
            'viewProjection', 'previousViewProjection', 'inverseViewProjection'):
        if meta.get(key) != target[key]:
            raise ValueError('Seed-study camera/geometry mismatch: '+key)
    assess_frame(meta, False, False)
    state = meta['nrdLastEvaluation']
    if state.get('recorded') is not True or state['frameIndex'] != frames:
        raise ValueError('Wrong seed-study NRD history checkpoint')
    comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    if common is not None and comparable != common:
        raise ValueError('Mismatched seed-study resource cohort')
    return comparable


def capture(output, resource, seed, frames, target, common):
    label = 's'+str(seed)+'-f'+str(frames)+'-'+resource
    path, log = output/(label+'.ptbuf'), output/(label+'.log')
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(output/'scene.json'),
        '-RenderPreset', str(output/'render-preset.json'), '-EnablePathTracing', '-PathTracingSeed', str(seed),
        '-DebugPreviewResource', 'PathTracing.'+resource, '-CapturePath', str(path),
        '-CaptureAfterFrames', str(frames), '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=180)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0:
        raise ValueError('Seed-study capture failed: '+label)
    record = dict(label=label, resource=resource, seed=seed, frames=frames, command=command,
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
    meta, image, runtime = read_evidence(output, label, record)
    common = check_capture(meta, runtime, resource, seed, frames, target, common)
    record.update(metadata=meta, diagnostics=runtime)
    return image, record, common


def measure(buffers, references, regions, seed, frames):
    if set(buffers) != set(RESOURCES) or set(regions) != EXPECTED:
        raise ValueError('Incomplete seed-study resource/ROI inventory')
    shape = buffers['NrdSceneColor'].shape
    if (len(shape) != 3 or shape[-1] != 4 or any(image.shape != shape or not np.isfinite(image).all()
            for image in (*buffers.values(), *references))):
        raise ValueError('Malformed seed-study images')
    if any(mask.shape != shape[:2] or mask.dtype != bool for mask in regions.values()):
        raise ValueError('Malformed fixed ROI mask')
    rows = []
    for name in sorted(regions):
        mask = regions[name]
        diagnostics = buffers['NrdCompositionDiagnostics'][mask]
        if not len(diagnostics) or not (diagnostics[:, 0] == 1).all() or not (diagnostics[:, 1] == 0).all():
            raise ValueError('Seed-study ROI did not use valid NRD composition: '+name)
        result = assess_roi(*(image[mask, :3] for image in
            (buffers['NrdSceneColor'], buffers['SceneColor'], *references)))
        rows.append(dict(result, roi=name, seed=seed, frames=frames))
    roi_verdict(rows)
    return rows


def summarize(rows):
    if any(type(row.get('seed')) is not int or type(row.get('frames')) is not int or
            not isinstance(row.get('roi'), str) for row in rows):
        raise ValueError('Malformed seed/frame/ROI identity')
    expected = {(seed, frames, name) for seed in SEEDS for frames in FRAMES for name in EXPECTED}
    labels = [(row.get('seed'), row.get('frames'), row.get('roi')) for row in rows]
    if len(labels) != len(expected) or set(labels) != expected:
        raise ValueError('Incomplete, duplicate or unexpected seed/frame/ROI results')
    for seed in SEEDS:
        for frames in FRAMES:
            roi_verdict([row for row in rows if row['seed'] == seed and row['frames'] == frames])
    summaries = []
    for name in sorted(EXPECTED):
        fixed_reference, fixed_pair = None, None
        for frames in FRAMES:
            group = [next(row for row in rows if (row['seed'], row['frames'], row['roi']) ==
                (seed, frames, name)) for seed in SEEDS]
            for row in group:
                if row.get('qualified') is not True:
                    raise ValueError('Fixed reference did not qualify for seed study')
                reference = np.asarray(row.get('referenceMeanRgb'), dtype=float)
                nrd = np.asarray(row.get('nrdMeanRgb'), dtype=float)
                if (reference.shape != (3,) or nrd.shape != (3,) or not np.isfinite([reference, nrd]).all() or
                        (reference < 0).any() or (nrd < 0).any() or np.linalg.norm(reference) <= 1e-6):
                    raise ValueError('Missing/invalid seed-study mean RGB')
                if fixed_reference is None:
                    fixed_reference, fixed_pair = reference, row['referencePairNrmse']
                if not np.array_equal(reference, fixed_reference) or row['referencePairNrmse'] != fixed_pair:
                    raise ValueError('Seed study does not share one fixed reference')
                bias = float(np.linalg.norm(nrd-reference)/np.linalg.norm(reference))
                if not np.isclose(bias, row['meanRgbBias'], rtol=1e-10, atol=1e-12):
                    raise ValueError('Seed-study mean bias does not match RGB evidence')
            means = np.asarray([row['nrdMeanRgb'] for row in group])
            signed_luminance = ((means-fixed_reference) @ LUMINANCE)/float(fixed_reference @ LUMINANCE)
            summaries.append(dict(roi=name, frames=frames, seeds=list(SEEDS),
                passedSeeds=[row['seed'] for row in group if row['passed']],
                biasFailedSeeds=[row['seed'] for row in group if not row['checks']['meanRgbBias']],
                rmseFailedSeeds=[row['seed'] for row in group if not row['checks']['noRmseRegression']],
                minimumMeanRgbBias=min(row['meanRgbBias'] for row in group),
                maximumMeanRgbBias=max(row['meanRgbBias'] for row in group),
                ensembleMeanRgbBias=float(np.linalg.norm(means.mean(axis=0)-fixed_reference)/np.linalg.norm(fixed_reference)),
                signedLuminanceErrors=signed_luminance.tolist(),
                darkerSeeds=[seed for seed, error in zip(SEEDS, signed_luminance) if error < 0],
                allSeedsAllChannelsDarker=bool((means < fixed_reference).all())))
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if (output == source or source in output.parents or output == FIXTURE or FIXTURE in output.parents or
            (output.exists() and (not output.is_dir() or any(output.iterdir())))):
        parser.error('Output must be a separate new/empty directory')
    artifacts = dict(identities())
    for name in ('isolate_nrd_glossy_bias.py', 'validate_nrd_camera_motion.py', 'validate_nrd_glossy_seeds.py'):
        path = Path(__file__).with_name(name)
        artifacts[str(path.relative_to(ROOT))] = sha(path)
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated', fullIntegrationReady=False,
        sourceEvidence=str(source), sourceReportSha256=sha(source/'report.json'), sourceEvidenceReexecuted=False,
        sourceRecordsRevalidated=0, freshGpuCaptures=0, artifactSha256=artifacts, seeds=list(SEEDS),
        checkpoints=list(FRAMES), limits=ROI_LIMITS, selection=SELECTION, masksReselected=False,
        records=[], rois=[], summaries=[], failures=[], subjectiveVerdict='not-repeated')
    write_json(output/'report.json', report)
    try:
        scene, preset, previous, target, regions, references, limitation = load_references(source)
        report.update(sourceRecordsRevalidated=18, historicalProvenanceLimitation=limitation)
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            write_json(output/name, value)
        report['fixtureSha256'] = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        for seed in SEEDS:
            for frames in FRAMES:
                buffers, common = {}, None
                for resource in RESOURCES:
                    image, record, common = capture(output, resource, seed, frames, target, common)
                    buffers[resource] = image
                    report['records'].append(record)
                    report['freshGpuCaptures'] = len(report['records'])
                    write_json(output/'report.json', report)
                if seed == 7 and frames == 30 and any(not np.array_equal(buffers[name], previous[name]) for name in RESOURCES):
                    raise ValueError('Seed-7 baseline differs from original evidence')
                rows = measure(buffers, references, regions, seed, frames)
                report['rois'].extend(rows)
                write_json(output/'report.json', report)
                focus = next(row for row in rows if row['roi'] == ROI)
                print('seed', seed, 'frames', frames, 'focus bias', focus['meanRgbBias'], focus['status'], flush=True)
        report['summaries'] = summarize(report['rois'])
        if (any(sha(ROOT/path) != digest for path, digest in artifacts.items()) or
                sha(source/'report.json') != report['sourceReportSha256'] or
                any(sha(output/name) != digest for name, digest in report['fixtureSha256'].items())):
            raise ValueError('Acquisition artifacts, source or fixture changed')
        passed = all(row['passed'] for row in report['rois'])
        report.update(status='completed-seed-study', qualityVerdict='passed-sampled-rois' if passed else 'failed-sampled-rois')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = ('Five predeclared candidate seeds, none equal reference seeds 11/29; fixed 14 masks. '
        'Descriptive shared-reference statistics, no formal independence/causality or general quality claim. '
        '120 elapsed frames retain SDK default 30-frame linear-history cap. Native has 30/120 samples; '
        'no equal-ray/time claim. Ensemble mean RGB bias is not mean per-seed bias or ensemble image RMSE. '
        'No settings changes or repeated human review.')
    write_json(output/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['qualityVerdict'] != 'passed-sampled-rois'


if __name__ == '__main__':
    raise SystemExit(main())
