"""Audit glossy signal arithmetic and observe fixed-ROI bias at longer history."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from refine_nrd_glossy_rois import (SAMPLES, SEEDS, identities, load_fixed, evaluate)
from requalify_nrd_glossy import read_evidence, check_command
from validate_nrd_composition import NAMES, assess_composition, expected_composition
from validate_nrd_disocclusion_quality import check_reference
from validate_nrd_glossy_rois import ROOT, FIXTURE, ROI_LIMITS, SELECTION, assess_roi, roi_verdict
from validate_nrd_camera_motion import assess_frame
from validate_part1 import sha, write_json

RESOURCES = NAMES + ('NrdInputDiffuse', 'NrdInputSpecular', 'CurrentFrameRadiance', 'Albedo')
LONG_RESOURCES = NAMES[:10]
ROI = 'dielectric-smooth/mirror-proxy'


def load_references(folder):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    source = Path(report['sourceEvidence']).resolve(strict=True)
    labels = ['reference-b4-'+str(seed) for seed in SEEDS]
    records = report.get('records', [])
    if (report.get('schemaVersion') != 1 or report.get('failures') != [] or
            report.get('artifactSha256') != identities() or report.get('sourceReportSha256') != sha(source/'report.json') or
            report.get('referenceSamples') != SAMPLES or report.get('referenceSeeds') != list(SEEDS) or
            report.get('maxBounces') != 4 or report.get('limits') != ROI_LIMITS or report.get('selection') != SELECTION or
            report.get('sourceRecordsRevalidated') != 16 or report.get('freshGpuCaptures') != 2 or
            report.get('candidateGpuCaptures') != 0 or report.get('masksReselected') is not False or
            report.get('sourceEvidenceReexecuted') is not False or report.get('fullIntegrationReady') is not False or
            report.get('subjectiveVerdict') != 'unanswered' or len(records) != 2 or
            {row.get('label') for row in records} != set(labels)):
        raise ValueError('Incomplete or changed refinement evidence')
    scene, preset, buffers, target, regions, _, limitation = load_fixed(source)
    source_rows = json.loads((source/'report.json').read_text(encoding='utf-8'))['rois']
    masks = {row['roi']: dict(file=row['maskFile'], sha256=row['maskSha256'],
        logicalSha256=row['logicalMaskSha256']) for row in source_rows}
    if report.get('fixedMasks') != masks:
        raise ValueError('Refinement mask identity changed')
    references = []
    for seed in SEEDS:
        label = 'reference-b4-'+str(seed)
        record = next(row for row in records if row['label'] == label)
        command = record.get('command', [])
        if not command or command[-1] != '-DisableVSync':
            raise ValueError('Missing reference VSync override')
        scene_path, preset_path = folder/(label+'-scene.json'), folder/(label+'-preset.json')
        check_command(dict(record, command=command[:-1]), folder, label, 'SceneColor', scene_path, preset_path, SAMPLES)
        expected_scene, expected_preset = copy.deepcopy(scene), copy.deepcopy(preset)
        expected_scene['renderPreset'] = label+'-preset.json'
        expected_preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False,
            randomSeed=seed, maxBounces=4)
        for path, value, key in ((scene_path, expected_scene, 'sceneSha256'), (preset_path, expected_preset, 'presetSha256')):
            if sha(path) != record.get(key) or json.loads(path.read_text(encoding='utf-8')) != value:
                raise ValueError('Fresh reference settings changed')
        meta, image, diagnostics = read_evidence(folder, label, record)
        if meta != record.get('metadata'):
            raise ValueError('Fresh reference metadata changed')
        check_reference(meta, diagnostics, target, seed, environment_mode=7, sample_count=SAMPLES, max_bounces=4)
        references.append(image)
    rows = evaluate(buffers, references, regions)
    if report.get('rois') != rows or report.get('status') != roi_verdict(rows):
        raise ValueError('Refined ROI aggregate differs from actual evidence')
    return scene, preset, buffers, target, regions, references, limitation


def check_capture(meta, diagnostics, name, frames, target, common):
    expected_format = 10 if name in ('SceneColor', 'Albedo') else 2
    if (name not in RESOURCES or meta.get('resource') != 'PathTracing.'+name or meta.get('format') != expected_format or
            meta.get('sampleStartIndex') != frames or meta.get('primarySampling') != 'pixel-center' or
            meta.get('samplesPerFrame') != 1 or meta.get('randomSeed') != 7 or
            meta.get('denoiserDistanceDiagnostics') is not True or meta.get('nrdPreviewRequested') is not True or
            meta.get('nrdPresentation') != 'NRD RELAX diagnostic (per-pixel Native fallback)' or meta.get('nrdFailure') != ''):
        raise ValueError('Wrong diagnostic capture identity/state')
    expected = dict(accumulatedSamples=frames, targetSamples=0, maxBounces=4, randomSeed=7,
        samplesPerFrame=1, environmentEnabled=True, environmentSamplingMode=7,
        directLightingEnabled=False, emissiveEnabled=False, russianRoulette=False,
        denoiserDistanceDiagnostics=True, nrdFailureCount=0)
    if any(diagnostics.get(key) != value for key, value in expected.items()):
        raise ValueError('Diagnostic capture transport/sample state differs')
    for key in ('width', 'height', 'rayTMin', 'rayTMax', 'normalBias', 'lensShift', 'viewProjection', 'inverseViewProjection'):
        if meta.get(key) != target[key]:
            raise ValueError('Diagnostic capture camera/geometry differs: '+key)
    assess_frame(meta, False, False)
    if meta['nrdLastEvaluation']['frameIndex'] != frames or meta['nrdLastEvaluation'].get('recorded') is not True:
        raise ValueError('Wrong NRD history checkpoint')
    comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    if common is not None and comparable != common:
        raise ValueError('Mismatched diagnostic capture cohort')
    return comparable


def capture(output, name, frames, target, common):
    label = 'f'+str(frames)+'-'+name
    path, log = output/(label+'.ptbuf'), output/(label+'.log')
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(output/'scene.json'),
        '-RenderPreset', str(output/'render-preset.json'), '-EnablePathTracing',
        '-DebugPreviewResource', 'PathTracing.'+name, '-CapturePath', str(path),
        '-CaptureAfterFrames', str(frames), '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=180)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0:
        raise ValueError('Diagnostic capture process failed: '+label)
    record = dict(label=label, resource=name, frames=frames, command=command,
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
    meta, image, diagnostics = read_evidence(output, label, record)
    common = check_capture(meta, diagnostics, name, frames, target, common)
    record.update(metadata=meta, diagnostics=diagnostics)
    return image, record, common


def arithmetic(buffers):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Incomplete signal isolation cohort')
    result = assess_composition(buffers)
    converted = buffers['NrdValidity'][..., 0] == 1
    admitted = buffers['NrdInputAdmission'][..., 0] == 1
    if not admitted.any() or not converted.any():
        raise ValueError('Missing converted/admitted signal coverage')
    for lobe in ('Diffuse', 'Specular'):
        if not np.array_equal(buffers['NrdInput'+lobe][admitted, :3], buffers['Nrd'+lobe+'Candidate'][admitted, :3]):
            raise ValueError('Admission/reconstruction changed radiance RGB: '+lobe)
    physical = ((buffers['PhysicalDiffuse'][..., :3] + buffers['PhysicalSpecular'][..., :3]) +
        buffers['StableRadiance'][..., :3]) + buffers['BackgroundRadiance'][..., :3]
    frame = buffers['CurrentFrameRadiance'][..., :3]
    bound = 16*np.abs(np.spacing(np.abs(frame).astype(np.float32)).astype(float)) + 1e-6
    if not (abs(physical-frame) <= bound).all():
        raise ValueError('Physical lobe/stable/background split does not reconstruct current radiance')
    result.update(admissionRgbPreserved=True, currentFrameSplitMaximumError=float(abs(physical-frame).max()))
    return result


def measure(buffers, native, references, mask):
    if not mask.any() or not (buffers['NrdCompositionDiagnostics'][mask, 0] == 1).all():
        raise ValueError('Fixed failing ROI must use NRD, not fallback')
    color, diagnostics = expected_composition(*(buffers[name] for name in NAMES[:8]))
    if not np.array_equal(buffers['NrdCompositionDiagnostics'], diagnostics):
        raise ValueError('Composition classification differs')
    bound = 8*np.abs(np.spacing(np.abs(color).astype(np.float32)).astype(float)) + 1e-6
    if not (abs(buffers['NrdSceneColor']-color) <= bound).all():
        raise ValueError('Composition arithmetic differs')
    row = assess_roi(*(image[mask, :3] for image in (buffers['NrdSceneColor'], native, *references)))
    row['restoredLobeMeanRgb'] = {lobe: (buffers['NrdOutput'+lobe][mask, :3] *
        buffers['Nrd'+lobe+'Factor'][mask, :3]).mean(axis=0, dtype=np.float64).tolist()
        for lobe in ('Diffuse', 'Specular')}
    row['stableMeanRgb'] = buffers['StableRadiance'][mask, :3].mean(axis=0, dtype=np.float64).tolist()
    row['backgroundMeanRgb'] = buffers['BackgroundRadiance'][mask, :3].mean(axis=0, dtype=np.float64).tolist()
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if (output == source or source in output.parents or output == FIXTURE or FIXTURE in output.parents or
            (output.exists() and (not output.is_dir() or any(output.iterdir())))):
        parser.error('Output must be a separate new/empty directory')
    artifacts = dict(identities(), **{str(Path(__file__).relative_to(ROOT)): sha(Path(__file__)),
        'Tests/PathTracing/validate_nrd_composition.py': sha(Path(__file__).with_name('validate_nrd_composition.py')),
        'Tests/PathTracing/validate_nrd_camera_motion.py': sha(Path(__file__).with_name('validate_nrd_camera_motion.py'))})
    report = dict(schemaVersion=1, status='running', fullIntegrationReady=False, qualityVerdict='not-evaluated',
        sourceEvidence=str(source), sourceReportSha256=sha(source/'report.json'), sourceEvidenceReexecuted=False,
        artifactSha256=artifacts, limits=ROI_LIMITS, roi=ROI, records=[], checkpoints=[], failures=[], subjectiveVerdict='unanswered')
    write_json(output/'report.json', report)
    try:
        scene, preset, previous, target, regions, references, limitation = load_references(source)
        report.update(sourceRecordsRevalidated=18, historicalProvenanceLimitation=limitation)
        write_json(output/'scene.json', scene)
        write_json(output/'render-preset.json', preset)
        report['fixtureSha256'] = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        for frames, names in ((30, RESOURCES), (120, LONG_RESOURCES)):
            buffers, common = {}, None
            for name in names:
                image, record, common = capture(output, name, frames, target, common)
                buffers[name] = image
                report['records'].append(record)
                write_json(output/'report.json', report)
                print('captured f'+str(frames)+' '+name, flush=True)
            if frames == 30:
                for name in ('Albedo', 'SceneColor', 'NrdSceneColor', 'NrdInputAdmission', 'NrdCompositionDiagnostics'):
                    if not np.array_equal(buffers[name], previous[name]):
                        raise ValueError('Thirty-frame baseline did not reproduce prior evidence: '+name)
                report['arithmetic'] = arithmetic(buffers)
            result = measure(buffers, buffers['SceneColor'], references, regions[ROI])
            report['checkpoints'].append(dict(frames=frames, assessment=result, metadata=common))
            write_json(output/'report.json', report)
        if any(sha(ROOT/path) != digest for path, digest in artifacts.items()):
            raise ValueError('Acquisition artifacts changed during execution')
        if (sha(source/'report.json') != report['sourceReportSha256'] or any(
                sha(output/name) != digest for name, digest in report['fixtureSha256'].items())):
            raise ValueError('Source or fixture changed during execution')
        report.update(status='completed-diagnostic', qualityVerdict='not-promoted')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = ('Single fixed seed/history comparison, not a causal attribution or general quality pass. '
        'Current input is one Monte Carlo sample, not a converged lobe reference. No denoiser settings changed. '
        '30/120 Native accumulate different sample counts; no equal-ray/time claim.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'completed-diagnostic'


if __name__ == '__main__':
    raise SystemExit(main())
