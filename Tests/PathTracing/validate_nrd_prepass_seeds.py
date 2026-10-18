"""Confirm diffuse-radius-5 sensitivity across five fixed seeds and all glossy ROIs."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import ROI
from requalify_nrd_glossy import read_evidence
from validate_nrd_prepass_sensitivity import load_bridge, command, check_profile
from validate_nrd_prepass_bridge import check_runtime
from validate_nrd_glossy_seeds import SEEDS, FRAMES, RESOURCES, check_capture, measure, summarize
from validate_nrd_glossy_rois import ROOT, EXPECTED, ROI_LIMITS
from validate_part1 import sha, write_json


def compare_rows(control, variant):
    summarize(control)
    summarize(variant)
    baseline = {(row['seed'], row['frames'], row['roi']): row for row in control}
    result = []
    for row in variant:
        old = baseline[row['seed'], row['frames'], row['roi']]
        if row['referenceMeanRgb'] != old['referenceMeanRgb'] or row['referencePairNrmse'] != old['referencePairNrmse']:
            raise ValueError('Control and variant do not share fixed references')
        result.append(dict(seed=row['seed'], frames=row['frames'], roi=row['roi'],
            meanRgbBiasDelta=row['meanRgbBias']-old['meanRgbBias'],
            nrdNrmseDelta=row['nrdNrmse']-old['nrdNrmse'],
            regression=bool(old['passed'] and not row['passed']),
            recovered=bool(not old['passed'] and row['passed']),
            passedBefore=old['passed'], passedAfter=row['passed']))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    bridge, exe = args.bridge.resolve(strict=True), args.executable.resolve(strict=True)
    output = args.output.resolve()
    if any(output == path or path in output.parents for path in (bridge, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a new separate directory')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {str(path): sha(path) for path in (exe, Path(__file__),
        Path(__file__).with_name('validate_nrd_prepass_sensitivity.py'), bridge/'report.json',
        exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso')}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated',
        bridgeEvidence=str(bridge), artifactSha256=artifacts, profile='diffuse-5', seeds=list(SEEDS),
        checkpoints=list(FRAMES), limits=ROI_LIMITS, masksReselected=False, freshGpuCaptures=0,
        records=[], controlRois=[], rois=[], summaries=[], comparisons=[], failures=[],
        fullIntegrationReady=False, subjectiveVerdict='not-repeated',
        limitation='Static fixture and five predeclared seeds, descriptive shared-reference statistics only. '
            '120 elapsed frames retain the default 30-frame history cap. Defaults unchanged.')
    write_json(output/'report.json', report)
    try:
        scene, preset, target, regions, references, _ = load_bridge(bridge, exe)
        bridge_report = json.loads((bridge/'report.json').read_text(encoding='utf-8'))
        source = Path(bridge_report['sourceEvidence']).resolve(strict=True)
        historical = json.loads((source/'report.json').read_text(encoding='utf-8'))
        report.update(controlEvidence=str(source), controlReportSha256=sha(source/'report.json'))
        if historical.get('status') != 'completed-seed-study' or historical.get('failures') != []:
            raise ValueError('Invalid historical control study')
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            if (json.loads((source/name).read_text(encoding='utf-8')) != value or
                    sha(source/name) != historical['fixtureSha256'].get(name)):
                raise ValueError('Historical control fixture changed')
            write_json(output/name, value)
        fixtures = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        control = {}
        for seed in SEEDS:
            for frames in FRAMES:
                buffers, common = {}, None
                for name in RESOURCES:
                    old = next(row for row in historical['records'] if
                        (row['seed'], row['frames'], row['resource']) == (seed, frames, name))
                    label = f's{seed}-f{frames}-{name}'
                    expected = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(source/'scene.json'),
                        '-RenderPreset', str(source/'render-preset.json'), '-EnablePathTracing', '-PathTracingSeed',
                        str(seed), '-DebugPreviewResource', 'PathTracing.'+name, '-CapturePath',
                        str(source/(label+'.ptbuf')), '-CaptureAfterFrames', str(frames), '-LogToFile',
                        str(source/(label+'.log')), '-ExitAfterCapture', '-DisableVSync']
                    if old.get('command') != expected:
                        raise ValueError('Historical control command changed')
                    meta, image, runtime = read_evidence(source, label, old)
                    if meta != old['metadata']:
                        raise ValueError('Historical control metadata changed')
                    common = check_capture(meta, runtime, name, seed, frames, target, common)
                    buffers[name] = image
                    control[seed, frames, name] = (image, meta, runtime)
                report['controlRois'].extend(measure(buffers, references, regions, seed, frames))
        if report['controlRois'] != historical['rois'] or summarize(report['controlRois']) != historical['summaries']:
            raise ValueError('Historical control assessment changed')
        report['sourceRecordsRevalidated'] = 68
        for seed in SEEDS:
            for frames in FRAMES:
                buffers, common = {}, None
                for name in RESOURCES:
                    label = f'diffuse-5-s{seed}-f{frames}-{name}'
                    cmd = command(exe, output, seed, frames, name, 'diffuse-5', label)
                    subprocess.run(cmd, cwd=ROOT, check=True, timeout=180)
                    record = dict(label=label, resource=name, seed=seed, frames=frames, command=cmd,
                        sha256=sha(output/(label+'.ptbuf')), logSha256=sha(output/(label+'.log')), d3d12Errors=0)
                    meta, image, runtime = read_evidence(output, label, record)
                    record.update(metadata=meta, diagnostics=runtime)
                    report['records'].append(record)
                    report['freshGpuCaptures'] = len(report['records'])
                    check_profile(meta, 'diffuse-5')
                    common = check_capture(meta, runtime, name, seed, frames, target, common)
                    old_image, old_meta, old_runtime = control[seed, frames, name]
                    check_runtime(old_runtime, runtime, output/(label+'.ptbuf'))
                    if {k: v for k, v in meta.items() if k != 'nrdPrepass'} != old_meta:
                        raise ValueError('Variant metadata differs beyond declared prepass profile')
                    if name == 'SceneColor' and not np.array_equal(image, old_image):
                        raise ValueError('Variant changed Native transport')
                    buffers[name] = image
                    write_json(output/'report.json', report)
                rows = measure(buffers, references, regions, seed, frames)
                report['rois'].extend(rows)
                write_json(output/'report.json', report)
                focus = next(row for row in rows if row['roi'] == ROI)
                print(seed, frames, 'bias', focus['meanRgbBias'], 'passed', sum(row['passed'] for row in rows), '/14',
                    flush=True)
        report['summaries'] = summarize(report['rois'])
        report['comparisons'] = compare_rows(report['controlRois'], report['rois'])
        if any(sha(Path(path)) != digest for path, digest in artifacts.items()) or any(
                sha(output/name) != digest for name, digest in fixtures.items()) or (
                sha(source/'report.json') != report['controlReportSha256']):
            raise ValueError('Study artifacts changed')
        report.update(status='completed-prepass-seed-study', fixtureSha256=fixtures,
            qualityVerdict='passed-sampled-rois' if all(row['passed'] for row in report['rois']) else 'failed-sampled-rois')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['status'] != 'completed-prepass-seed-study'


if __name__ == '__main__':
    raise SystemExit(main())
