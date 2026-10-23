"""Compare alternate diagnostic build control outputs with historical seed evidence."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import load_references
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_seeds import SEEDS, FRAMES, RESOURCES, check_capture, measure, summarize
from validate_nrd_glossy_rois import ROOT
from validate_part1 import sha, write_json


def compare(old, new):
    if old.shape != new.shape or not np.isfinite(old).all() or not np.isfinite(new).all():
        raise ValueError('Invalid bridge images')
    return dict(equal=bool(np.array_equal(old, new)),
        maximumAbsoluteError=float(np.max(np.abs(old.astype(np.float64)-new))),
        changedValues=int(np.count_nonzero(old != new)))


def check_prepass(meta):
    if meta.get('nrdPrepass') != dict(profile='control', applied=True, diffuseRadius=30, specularRadius=50):
        raise ValueError('Control prepass was not applied')
    return {key: value for key, value in meta.items() if key != 'nrdPrepass'}


def inventory(records, resources):
    expected = {(seed, frames, name) for seed in SEEDS for frames in FRAMES for name in resources}
    actual = [(row.get('seed'), row.get('frames'), row.get('resource')) for row in records]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError('Incomplete, duplicate or unexpected bridge inventory')


def check_runtime(old, new, path):
    if new.get('capturePath') != str(path):
        raise ValueError('Bridge runtime capture destination mismatch')
    timing = ('gpuTimeMs', 'primarySamplesPerSecond')
    for key in timing:
        for runtime in (old, new):
            value = runtime.get(key)
            if not isinstance(value, (float, int)) or not np.isfinite(value) or value <= 0:
                raise ValueError('Invalid bridge runtime timing')
    excluded = set(timing) | {'capturePath'}
    if {k: v for k, v in old.items() if k not in excluded} != {
            k: v for k, v in new.items() if k not in excluded}:
        raise ValueError('Bridge runtime differs beyond measured timing and validated output path')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--references', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, refs, exe = (path.resolve(strict=True) for path in
        (args.source, args.references, args.executable))
    output = args.output.resolve()
    if any(output == path or path in output.parents for path in (source, refs, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a new separate directory')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {str(path): sha(path) for path in (exe, Path(__file__), source/'report.json', refs/'report.json',
        exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso')}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated',
        sourceEvidence=str(source), referenceEvidence=str(refs), executable=str(exe), artifactSha256=artifacts,
        sourceRecordsRevalidated=0, freshGpuCaptures=0, records=[], comparisons=[], failures=[],
        fullIntegrationReady=False,
        runtimeComparisonExclusions=['gpuTimeMs', 'primarySamplesPerSecond', 'capturePath'],
        limitation='Static fixture only; no temporal/general quality claim.')
    write_json(output/'report.json', report)
    try:
        scene, preset, _, target, regions, references, _ = load_references(refs)
        previous = json.loads((source/'report.json').read_text(encoding='utf-8'))
        if (previous.get('status') != 'completed-seed-study' or previous.get('failures') != [] or
                Path(previous['sourceEvidence']).resolve() != refs or
                previous.get('sourceReportSha256') != sha(refs/'report.json')):
            raise ValueError('Wrong historical seed study')
        for path, digest in previous['artifactSha256'].items():
            if sha(ROOT/path) != digest:
                raise ValueError('Historical seed artifact changed: '+path)
        inventory(previous['records'], RESOURCES)
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            if (json.loads((source/name).read_text(encoding='utf-8')) != value or
                    previous['fixtureSha256'].get(name) != sha(source/name)):
                raise ValueError('Historical fixture changed')
            write_json(output/name, value)
        fixture_hashes = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        historical, rows = {}, []
        for seed in SEEDS:
            for frames in FRAMES:
                buffers, common = {}, None
                for name in RESOURCES:
                    record = next(row for row in previous['records'] if
                        (row['seed'], row['frames'], row['resource']) == (seed, frames, name))
                    label = f's{seed}-f{frames}-{name}'
                    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(source/'scene.json'),
                        '-RenderPreset', str(source/'render-preset.json'), '-EnablePathTracing',
                        '-PathTracingSeed', str(seed), '-DebugPreviewResource', 'PathTracing.'+name,
                        '-CapturePath', str(source/(label+'.ptbuf')), '-CaptureAfterFrames', str(frames),
                        '-LogToFile', str(source/(label+'.log')), '-ExitAfterCapture', '-DisableVSync']
                    if record.get('command') != command:
                        raise ValueError('Historical command changed')
                    meta, image, runtime = read_evidence(source, label, record)
                    if meta != record.get('metadata'):
                        raise ValueError('Historical metadata changed')
                    common = check_capture(meta, runtime, name, seed, frames, target, common)
                    buffers[name] = image
                    historical[seed, frames, name] = (image, meta, runtime)
                rows.extend(measure(buffers, references, regions, seed, frames))
        if rows != previous['rois'] or summarize(rows) != previous['summaries']:
            raise ValueError('Historical measurements changed')
        report['sourceRecordsRevalidated'] = 48
        for seed in SEEDS:
            for frames in FRAMES:
                for name in ('SceneColor', 'NrdSceneColor'):
                    label = f's{seed}-f{frames}-{name}'
                    path, log = output/(label+'.ptbuf'), output/(label+'.log')
                    command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset',
                        str(output/'render-preset.json'), '-EnablePathTracing', '-PathTracingSeed', str(seed),
                        '-NrdPrepassProfile', 'control', '-DebugPreviewResource', 'PathTracing.'+name,
                        '-CapturePath', str(path), '-CaptureAfterFrames', str(frames),
                        '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
                    subprocess.run(command, cwd=ROOT, check=True, timeout=180)
                    record = dict(label=label, resource=name, seed=seed, frames=frames, command=command,
                        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
                    meta, image, runtime = read_evidence(output, label, record)
                    record.update(metadata=meta, diagnostics=runtime)
                    report['records'].append(record)
                    report['freshGpuCaptures'] = len(report['records'])
                    clean = check_prepass(meta)
                    check_capture(clean, runtime, name, seed, frames, target, None)
                    old_image, old_meta, old_runtime = historical[seed, frames, name]
                    check_runtime(old_runtime, runtime, path)
                    if clean != old_meta:
                        raise ValueError('Bridge runtime/metadata differs beyond declared additive prepass metadata')
                    result = dict(compare(old_image, image), seed=seed, frames=frames, resource=name)
                    report['comparisons'].append(result)
                    write_json(output/'report.json', report)
                    print(label, result, flush=True)
        inventory(report['records'], ('SceneColor', 'NrdSceneColor'))
        if any(sha(Path(path)) != digest for path, digest in artifacts.items()) or any(
                sha(output/name) != digest for name, digest in fixture_hashes.items()):
            raise ValueError('Bridge artifacts changed during execution')
        report['fixtureSha256'] = fixture_hashes
        report['status'] = 'passed-static-control-bridge' if all(
            row['equal'] for row in report['comparisons']) else 'failed-static-control-bridge'
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-static-control-bridge'


if __name__ == '__main__':
    raise SystemExit(main())
