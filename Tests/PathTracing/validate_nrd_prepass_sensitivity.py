"""Seed-7 fixed-ROI sensitivity study; no default changes or general quality claim."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import load_references, ROI
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_seeds import FRAMES, RESOURCES, check_capture, measure
from validate_nrd_prepass_bridge import check_prepass, check_runtime, compare, inventory
from validate_nrd_glossy_rois import ROOT, EXPECTED, ROI_LIMITS
from validate_part1 import sha, write_json

PROFILES = {'control': (30, 50), 'specular-10': (30, 10), 'diffuse-5': (5, 50)}


def command(exe, folder, seed, frames, name, profile, label):
    return [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'render-preset.json'),
        '-EnablePathTracing', '-PathTracingSeed', str(seed), '-NrdPrepassProfile', profile,
        '-DebugPreviewResource', 'PathTracing.'+name, '-CapturePath', str(folder/(label+'.ptbuf')),
        '-CaptureAfterFrames', str(frames), '-LogToFile', str(folder/(label+'.log')),
        '-ExitAfterCapture', '-DisableVSync']


def check_profile(meta, profile):
    diffuse, specular = PROFILES[profile]
    if meta.get('nrdPrepass') != dict(profile=profile, applied=True,
            diffuseRadius=diffuse, specularRadius=specular):
        raise ValueError('Wrong applied sensitivity profile')


def summarize(rows):
    expected = {(profile, frames, roi) for profile in PROFILES for frames in FRAMES for roi in EXPECTED}
    actual = [(row.get('profile'), row.get('frames'), row.get('roi')) for row in rows]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError('Incomplete/duplicate sensitivity ROI results')
    comparisons = []
    for profile in PROFILES:
        if profile == 'control':
            continue
        for frames in FRAMES:
            for name in sorted(EXPECTED):
                control = next(row for row in rows if
                    (row['profile'], row['frames'], row['roi']) == ('control', frames, name))
                variant = next(row for row in rows if
                    (row['profile'], row['frames'], row['roi']) == (profile, frames, name))
                comparisons.append(dict(profile=profile, frames=frames, roi=name,
                    meanRgbBiasDelta=variant['meanRgbBias']-control['meanRgbBias'],
                    passedBefore=control['passed'], passedAfter=variant['passed'],
                    regression=bool(control['passed'] and not variant['passed']),
                    recovered=bool(not control['passed'] and variant['passed'])))
    return comparisons


def load_bridge(folder, exe):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    if (report.get('status') != 'passed-static-control-bridge' or report.get('failures') != [] or
            report.get('freshGpuCaptures') != 20 or report.get('sourceRecordsRevalidated') != 48 or
            Path(report['executable']).resolve() != exe):
        raise ValueError('Missing successful control bridge')
    for path, digest in report['artifactSha256'].items():
        if sha(Path(path)) != digest:
            raise ValueError('Bridge artifact changed: '+path)
    refs = Path(report['referenceEvidence']).resolve(strict=True)
    scene, preset, _, target, regions, references, _ = load_references(refs)
    for name, value in (('scene.json', scene), ('render-preset.json', preset)):
        if (report['fixtureSha256'].get(name) != sha(folder/name) or
                json.loads((folder/name).read_text(encoding='utf-8')) != value):
            raise ValueError('Bridge fixture changed')
    source = Path(report['sourceEvidence']).resolve(strict=True)
    history = json.loads((source/'report.json').read_text(encoding='utf-8'))
    for path, digest in history['artifactSha256'].items():
        if sha(ROOT/path) != digest:
            raise ValueError('Historical artifact changed')
    inventory(history['records'], RESOURCES)
    inventory(report['records'], ('SceneColor', 'NrdSceneColor'))
    baseline, comparisons = {}, []
    for record in report['records']:
        seed, frames, name = record['seed'], record['frames'], record['resource']
        label = f's{seed}-f{frames}-{name}'
        if record.get('command') != command(exe, folder, seed, frames, name, 'control', label):
            raise ValueError('Bridge capture command changed')
        meta, image, runtime = read_evidence(folder, label, record)
        clean = check_prepass(meta)
        check_capture(clean, runtime, name, seed, frames, target, None)
        old = next(row for row in history['records'] if
            (row['seed'], row['frames'], row['resource']) == (seed, frames, name))
        old_meta, old_image, old_runtime = read_evidence(source, label, old)
        check_runtime(old_runtime, runtime, folder/(label+'.ptbuf'))
        if meta != record['metadata'] or clean != old_meta:
            raise ValueError('Bridge metadata changed')
        result = dict(compare(old_image, image), seed=seed, frames=frames, resource=name)
        if not result['equal']:
            raise ValueError('Bridge image no longer matches historical image')
        comparisons.append(result)
        if seed == 7:
            baseline[frames, name] = (image, meta, runtime)
    if comparisons != report['comparisons']:
        raise ValueError('Bridge comparison summary changed')
    return scene, preset, target, regions, references, baseline


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
    artifacts = {str(path): sha(path) for path in (exe, Path(__file__), bridge/'report.json',
        exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso')}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated',
        bridgeEvidence=str(bridge), artifactSha256=artifacts, profiles=PROFILES, seed=7, checkpoints=list(FRAMES),
        limits=ROI_LIMITS, masksReselected=False, freshGpuCaptures=0, records=[], rois=[], comparisons=[], failures=[],
        fullIntegrationReady=False, subjectiveVerdict='not-repeated',
        limitation='One seed, static fixture; descriptive sensitivity only. Five-seed follow-up required. '
            '120 elapsed frames retain the SDK default 30-frame history cap. Defaults unchanged.')
    write_json(output/'report.json', report)
    try:
        scene, preset, target, regions, references, baseline = load_bridge(bridge, exe)
        report['bridgeCapturesRevalidated'] = 20
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            write_json(output/name, value)
        fixture = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        for profile in PROFILES:
            for frames in FRAMES:
                buffers, common = {}, None
                for name in RESOURCES:
                    label = f'{profile}-s7-f{frames}-{name}'
                    cmd = command(exe, output, 7, frames, name, profile, label)
                    subprocess.run(cmd, cwd=ROOT, check=True, timeout=180)
                    record = dict(label=label, resource=name, seed=7, frames=frames, profile=profile, command=cmd,
                        sha256=sha(output/(label+'.ptbuf')), logSha256=sha(output/(label+'.log')), d3d12Errors=0)
                    meta, image, runtime = read_evidence(output, label, record)
                    record.update(metadata=meta, diagnostics=runtime)
                    report['records'].append(record)
                    report['freshGpuCaptures'] = len(report['records'])
                    check_profile(meta, profile)
                    common = check_capture(meta, runtime, name, 7, frames, target, common)
                    if name in ('SceneColor', 'NrdSceneColor'):
                        old_image, old_meta, old_runtime = baseline[frames, name]
                        check_runtime(old_runtime, runtime, output/(label+'.ptbuf'))
                        if {k: v for k, v in meta.items() if k != 'nrdPrepass'} != check_prepass(old_meta):
                            raise ValueError('Sensitivity capture metadata differs beyond profile')
                        if (name == 'SceneColor' or profile == 'control') and not np.array_equal(old_image, image):
                            raise ValueError('Native transport or control NRD output changed')
                    buffers[name] = image
                    write_json(output/'report.json', report)
                rows = [dict(row, profile=profile) for row in measure(buffers, references, regions, 7, frames)]
                report['rois'].extend(rows)
                write_json(output/'report.json', report)
                focus = next(row for row in rows if row['roi'] == ROI)
                print(profile, frames, 'focus bias', focus['meanRgbBias'], 'passed',
                    sum(row['passed'] for row in rows), '/14', flush=True)
        report['comparisons'] = summarize(report['rois'])
        if any(sha(Path(path)) != digest for path, digest in artifacts.items()) or any(
                sha(output/name) != digest for name, digest in fixture.items()):
            raise ValueError('Sensitivity artifacts changed')
        report.update(status='completed-sensitivity-study', fixtureSha256=fixture,
            qualityVerdict='passed-sampled-rois' if all(row['passed'] for row in report['rois']) else 'failed-sampled-rois')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['status'] != 'completed-sensitivity-study'


if __name__ == '__main__':
    raise SystemExit(main())
