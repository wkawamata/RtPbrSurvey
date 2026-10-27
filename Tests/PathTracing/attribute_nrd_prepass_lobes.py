"""Attribute control-to-diffuse-5 changes, without claiming per-lobe ground truth."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from isolate_nrd_glossy_bias import (RESOURCES, LONG_RESOURCES, ROI, check_capture, arithmetic,
    measure as assess_focus)
from validate_nrd_prepass_sensitivity import load_bridge, command, check_profile
from validate_nrd_prepass_bridge import check_runtime
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_seeds import measure as assess_rois, RESOURCES as ROI_RESOURCES
from validate_nrd_glossy_rois import ROOT
from validate_nrd_composition import expected_composition, NAMES
from validate_part1 import sha, write_json


def components(buffers):
    if not set(LONG_RESOURCES).issubset(buffers):
        raise ValueError('Missing lobe composition inputs')
    color, diagnostics = expected_composition(*(buffers[name] for name in NAMES[:8]))
    actual = buffers['NrdSceneColor']
    bound = 8*np.abs(np.spacing(np.abs(color).astype(np.float32)).astype(float))+1e-6
    if not np.array_equal(diagnostics, buffers['NrdCompositionDiagnostics']) or not (abs(color-actual) <= bound).all():
        raise ValueError('Lobe decomposition does not match actual GPU composition')
    mask = buffers['NrdInputAdmission']
    result = {lobe: np.where(mask[..., channel:channel+1] == 1, 0,
        buffers['NrdOutput'+lobe][..., :3]*buffers['Nrd'+lobe+'Factor'][..., :3])
        for lobe, channel in (('Diffuse', 2), ('Specular', 3))}
    result.update(Stable=buffers['StableRadiance'][..., :3], Background=buffers['BackgroundRadiance'][..., :3])
    return result


def attribute(control, variant, roi):
    if roi.dtype != bool or not roi.any():
        raise ValueError('Invalid fixed attribution ROI')
    for buffers in (control, variant):
        diag = buffers['NrdCompositionDiagnostics'][roi]
        if not (diag[:, 0] == 1).all() or not (diag[:, 1] == 0).all():
            raise ValueError('Attribution ROI used fallback')
    old, new = components(control), components(variant)
    if not np.array_equal(control['SceneColor'], variant['SceneColor']):
        raise ValueError('Native transport changed')
    rows = {}
    summed = np.zeros((int(roi.sum()), 3), dtype=np.float64)
    for name in old:
        delta = new[name][roi].astype(float)-old[name][roi]
        summed += delta
        rows[name] = dict(controlMeanRgb=old[name][roi].mean(axis=0, dtype=float).tolist(),
            variantMeanRgb=new[name][roi].mean(axis=0, dtype=float).tolist(),
            deltaMeanRgb=delta.mean(axis=0).tolist(), maximumAbsoluteChange=float(abs(delta).max()))
    total = variant['NrdSceneColor'][roi, :3].astype(float)-control['NrdSceneColor'][roi, :3]
    error = float(abs(summed-total).max())
    if error > 2e-6:
        raise ValueError('Lobe deltas do not explain final output delta')
    return dict(pixels=int(roi.sum()), components=rows, totalDeltaMeanRgb=total.mean(axis=0).tolist(),
        maximumDeltaClosureError=error, perLobeGroundTruthAvailable=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse-frame30', type=Path)
    args = parser.parse_args()
    bridge, source, exe = (p.resolve(strict=True) for p in (args.bridge, args.control, args.executable))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (bridge, source, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a separate new directory')
    output.mkdir(parents=True, exist_ok=True)
    paths = [exe, Path(__file__), source/'report.json', bridge/'report.json',
        Path(__file__).with_name('validate_nrd_prepass_sensitivity.py'),
        exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso']
    artifacts = {str(p): sha(p) for p in paths}
    reuse = args.reuse_frame30.resolve(strict=True) if args.reuse_frame30 else None
    reuse_records = {}
    if reuse is not None:
        if output == reuse or reuse in output.parents:
            parser.error('Reuse evidence and output must be separate')
        partial = json.loads((reuse/'report.json').read_text(encoding='utf-8'))
        labels = [row.get('label') for row in partial.get('records', [])]
        if (partial.get('status') != 'failed-execution' or partial.get('failures') !=
                ['Incomplete seed-study resource/ROI inventory'] or len(labels) != 10 or
                set(labels) != {f'diffuse-5-s7-f30-{name}' for name in LONG_RESOURCES}):
            parser.error('Reuse supports only the fully acquired, pre-aggregation frame-30 cohort')
        for path, digest in partial['artifactSha256'].items():
            if Path(path) != Path(__file__).resolve() and sha(Path(path)) != digest:
                parser.error('Reused acquisition artifact changed')
        artifacts[str(reuse/'report.json')] = sha(reuse/'report.json')
        reuse_records = {row['label']: row for row in partial['records']}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-promoted', controlEvidence=str(source),
        bridgeEvidence=str(bridge), artifactSha256=artifacts, freshGpuCaptures=0, records=[], checkpoints=[],
        failures=[], fullIntegrationReady=False, subjectiveVerdict='not-repeated',
        limitation='Seed 7 static sensitivity attribution only. No per-lobe reference exists here; '
            'a lobe response is not proof of lobe bias, input correctness, or the NRD quality ceiling.')
    write_json(output/'report.json', report)
    if reuse is not None:
        report.update(reusedFrame30Evidence=str(reuse), reusedCapturesRevalidated=0,
            reusedAcquisitionRunnerSha256=partial['artifactSha256'][str(Path(__file__).resolve())])
    try:
        scene, preset, target, regions, references, baseline = load_bridge(bridge, exe)
        previous = json.loads((source/'report.json').read_text(encoding='utf-8'))
        refs = Path(previous['sourceEvidence']).resolve(strict=True)
        bridge_report = json.loads((bridge/'report.json').read_text(encoding='utf-8'))
        if (previous.get('status') != 'completed-diagnostic' or previous.get('failures') != [] or
                refs != Path(bridge_report['referenceEvidence']).resolve() or
                previous.get('sourceReportSha256') != sha(refs/'report.json')):
            raise ValueError('Invalid control diagnostic evidence')
        for path, digest in previous['artifactSha256'].items():
            if sha(ROOT/path) != digest:
                raise ValueError('Control diagnostic artifact changed')
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            if (json.loads((source/name).read_text(encoding='utf-8')) != value or
                    sha(source/name) != previous['fixtureSha256'].get(name)):
                raise ValueError('Control diagnostic fixture changed')
            write_json(output/name, value)
        fixtures = {name: sha(output/name) for name in ('scene.json', 'render-preset.json')}
        if reuse is not None:
            for name, value in (('scene.json', scene), ('render-preset.json', preset)):
                if json.loads((reuse/name).read_text(encoding='utf-8')) != value:
                    raise ValueError('Reused frame-30 fixture changed')
        expected = {(30, name) for name in RESOURCES} | {(120, name) for name in LONG_RESOURCES}
        keys = [(row.get('frames'), row.get('resource')) for row in previous['records']]
        if len(keys) != len(expected) or set(keys) != expected:
            raise ValueError('Incomplete or duplicate control diagnostic inventory')
        controls = {}
        for frames, names in ((30, RESOURCES), (120, LONG_RESOURCES)):
            buffers, common = {}, None
            for name in names:
                label = f'f{frames}-{name}'
                record = next(row for row in previous['records'] if row['label'] == label)
                expected_cmd = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(source/'scene.json'),
                    '-RenderPreset', str(source/'render-preset.json'), '-EnablePathTracing', '-DebugPreviewResource',
                    'PathTracing.'+name, '-CapturePath', str(source/(label+'.ptbuf')), '-CaptureAfterFrames',
                    str(frames), '-LogToFile', str(source/(label+'.log')), '-ExitAfterCapture', '-DisableVSync']
                if record['command'] != expected_cmd:
                    raise ValueError('Control diagnostic command mismatch')
                meta, image, runtime = read_evidence(source, label, record)
                if meta != record['metadata']:
                    raise ValueError('Control diagnostic metadata changed')
                common = check_capture(meta, runtime, name, frames, target, common)
                buffers[name] = image
            assessment = assess_focus(buffers, buffers['SceneColor'], references, regions[ROI])
            if dict(frames=frames, assessment=assessment, metadata=common) != previous['checkpoints'][len(controls)]:
                raise ValueError('Control diagnostic aggregate changed')
            if frames == 30 and arithmetic(buffers) != previous['arithmetic']:
                raise ValueError('Control diagnostic arithmetic changed')
            for name in ('SceneColor', 'NrdSceneColor'):
                if not np.array_equal(buffers[name], baseline[frames, name][0]):
                    raise ValueError('Control diagnostic does not match verified bridge')
            controls[frames] = buffers
        report['controlDiagnosticCapturesRevalidated'] = 29
        for frames in (30, 120):
            buffers, common = {}, None
            for name in LONG_RESOURCES:
                label = f'diffuse-5-s7-f{frames}-{name}'
                folder = reuse if frames == 30 and reuse is not None else output
                cmd = command(exe, folder, 7, frames, name, 'diffuse-5', label)
                if folder == output:
                    subprocess.run(cmd, cwd=ROOT, check=True, timeout=180)
                    record = dict(label=label, resource=name, frames=frames, command=cmd,
                        sha256=sha(folder/(label+'.ptbuf')), logSha256=sha(folder/(label+'.log')), d3d12Errors=0)
                else:
                    record = dict(reuse_records[label])
                    if record['command'] != cmd:
                        raise ValueError('Reused frame-30 command changed')
                meta, image, runtime = read_evidence(folder, label, record)
                if folder != output and meta != record['metadata']:
                    raise ValueError('Reused frame-30 metadata changed')
                record.update(metadata=meta, diagnostics=runtime)
                record.update(evidenceFolder=str(folder), acquisition='fresh' if folder == output else 'revalidated-reuse')
                report['records'].append(record)
                report['freshGpuCaptures'] = sum(row['acquisition'] == 'fresh' for row in report['records'])
                report['reusedCapturesRevalidated'] = sum(row['acquisition'] == 'revalidated-reuse' for row in report['records'])
                check_profile(meta, 'diffuse-5')
                common = check_capture(meta, runtime, name, frames, target, common)
                check_runtime(baseline[frames, 'SceneColor'][2], runtime, folder/(label+'.ptbuf'))
                buffers[name] = image
                write_json(output/'report.json', report)
                print('captured', frames, name, flush=True)
            result = attribute(controls[frames], buffers, regions[ROI])
            result.update(frames=frames, roi=ROI,
                totalAssessment=assess_focus(buffers, buffers['SceneColor'], references, regions[ROI]),
                allRois=assess_rois({name: buffers[name] for name in ROI_RESOURCES}, references, regions, 7, frames))
            report['checkpoints'].append(result)
            write_json(output/'report.json', report)
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()) or any(
                sha(output/name) != digest for name, digest in fixtures.items()):
            raise ValueError('Attribution artifacts changed')
        report.update(status='completed-lobe-sensitivity-attribution', fixtureSha256=fixtures)
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'completed-lobe-sensitivity-attribution'


if __name__ == '__main__':
    raise SystemExit(main())
