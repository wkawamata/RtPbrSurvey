"""Reassess historical NRD outputs against the frozen 32768-sample component cohort."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np

from compare_nrd_component_references import (load_pairs, load_outputs, inventory, components, assess,
    summarize, joint_assessment, CHANNELS, PROFILES, FRAMES, BIAS_LIMIT)
from refine_nrd_component_references import summarize as reference_summary, check_capture, SAMPLES, SEEDS
from isolate_nrd_glossy_bias import load_references
from qualify_nrd_component_references import qualify
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_rois import EXPECTED
from validate_part1 import sha, write_json

REFINED_SHA = '12464ee792052aeba8616e528e68c17ec1f948f51526a124b88ef47bb508a8c8'


def validate_report(report):
    policy = dict(schemaVersion=1, status='completed-component-reference-refinement', samples=SAMPLES,
        seeds=list(SEEDS), samplesPerFrame=1, pairNrmseLimit=.03, windowMode='hidden', freshGpuCaptures=4,
        reusedTotalCapturesRevalidated=2, fullIntegrationReady=False, masksReselected=False, activeCapture=None,
        failures=[])
    if any(key not in report or report[key] != value for key, value in policy.items()):
        raise ValueError('Invalid refined-reference policy/state')
    inventory(report.get('records', []), {(c, s) for c in CHANNELS for s in SEEDS}, ('component', 'seed'))
    for row in report['records']:
        role = 'revalidated-total' if row['component'] == 'total' else 'fresh'
        if row.get('samples') != SAMPLES or row.get('acquisition') != role:
            raise ValueError('Wrong refined-reference acquisition role')
    summary = reference_summary(report.get('rois', []))
    verdict = 'qualified-all-components' if all(r['qualified'] for r in report['rois']) else 'partially-inconclusive'
    if report.get('summary') != summary or report.get('referenceVerdict') != verdict:
        raise ValueError('Refined-reference verdict does not match numerical evidence')


def load_refined(folder, pairs, source):
    if sha(folder/'report.json') != REFINED_SHA:
        raise ValueError('Expected frozen reviewed 32768-sample report')
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    validate_report(report)
    for path, digest in {**report['artifactSha256'], **report['fixtureSha256']}.items():
        if sha(Path(path)) != digest:
            raise ValueError('Refinement artifact or fixture changed: '+path)
    for evidence in (pairs, source):
        if report['artifactSha256'].get(str(evidence/'report.json')) != sha(evidence/'report.json'):
            raise ValueError('Refinement evidence chain changed')
    scene, preset, _, target, regions, total, _ = load_references(source)
    _, previous = load_pairs(pairs, regions, target, source)
    originals = json.loads((source/'report.json').read_text(encoding='utf-8'))['records']
    exes = [Path(p) for p in report['artifactSha256'] if Path(p).suffix == '.exe']
    if len(exes) != 1:
        raise ValueError('Ambiguous reference executable')
    references = {}
    fixtures = {}
    for record in report['records']:
        seed, component = record['seed'], record['component']
        if component == 'total':
            original = next(r for r in originals if r['label'] == 'reference-b4-'+str(seed))
            expected = dict(original, component='total', seed=seed, samples=SAMPLES,
                acquisition='revalidated-total', evidenceFolder=str(source))
            if record != expected:
                raise ValueError('Reused Total reference changed')
            references[seed, component] = total[SEEDS.index(seed)]
            continue
        capture_folder = folder/str(seed)
        if record['label'] != component or Path(record['evidenceFolder']) != capture_folder:
            raise ValueError('Wrong refined component identity')
        changed = copy.deepcopy(preset)
        changed['pathTracing'].update(samplesPerFrame=1, nrdPreview=False, denoiserDistanceDiagnostics=False,
            randomSeed=seed, debugOutput=3)
        for name, value in (('scene.json', scene), ('render-preset.json', changed)):
            if json.loads((capture_folder/name).read_text(encoding='utf-8')) != value:
                raise ValueError('Refined transport fixture changed')
            fixtures[str(capture_folder/name)] = sha(capture_folder/name)
        expected_command = [str(exes[0]), '-SceneFile', str(capture_folder/'scene.json'), '-RenderPreset',
            str(capture_folder/'render-preset.json'), '-EnablePathTracing', '-PathTracingReferenceComponent',
            component, '-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath',
            str(capture_folder/(component+'.ptbuf')), '-PathTracingSamples', str(SAMPLES), '-LogToFile',
            str(capture_folder/(component+'.log')), '-LogFPS', '1024', '-ExitAfterCapture', '-DisableVSync']
        if record['command'] != expected_command:
            raise ValueError('Refined acquisition command changed')
        meta, image, runtime = read_evidence(capture_folder, component, record)
        check_capture(meta, runtime, component, seed, target)
        if meta != record['metadata'] or not np.isfinite(image).all() or (image < 0).any() or not (image[..., 3] == 1).all():
            raise ValueError('Refined image/metadata changed')
        references[seed, component] = image
    if fixtures != report['fixtureSha256']:
        raise ValueError('Refined fixture inventory changed')
    qualifications = {}
    for component in CHANNELS:
        for roi, mask in regions.items():
            row = qualify(references[11, component], references[29, component], mask, component, roi)
            old = previous[component, roi]
            expected = dict(row, previous8192Status=old['status'], previous8192PairNrmse=old['referencePairNrmse'])
            actual = next(r for r in report['rois'] if (r['component'], r['roi']) == (component, roi))
            if actual != expected:
                raise ValueError('Refined qualification differs from raw reference images')
            qualifications[component, roi] = row
    return regions, references, qualifications


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('refined', 'pairs', 'source', 'attribution', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    refined, pairs, source, attribution = (p.resolve(strict=True) for p in
        (args.refined, args.pairs, args.source, args.attribution))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (refined, pairs, source, attribution)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a separate new folder')
    output.mkdir(parents=True, exist_ok=True)
    paths = [p/'report.json' for p in (refined, pairs, source, attribution)]
    paths += [Path(__file__), Path(__file__).with_name('compare_nrd_component_references.py'),
        Path(__file__).with_name('refine_nrd_component_references.py'),
        Path(__file__).with_name('qualify_nrd_component_references.py')]
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, records=[], summary=[], joint=[],
        failures=[], seed=7, referenceSamples=SAMPLES, meanRgbBiasTarget=BIAS_LIMIT, freshGpuCaptures=0,
        refinedReferenceCapturesRevalidated=0, historicalOutputCapturesRevalidated=0,
        fullIntegrationReady=False, qualityVerdict='not-promoted', subjectiveVerdict='not-repeated',
        limitation='Frozen historical single-seed static outputs, new reference cohort. Mean-bias point estimates '
        'only; no accumulated Native lobe RMSE comparison, SDK ceiling or new temporal/human acceptance claim.')
    try:
        regions, references, qualifications = load_refined(refined, pairs, source)
        report['refinedReferenceCapturesRevalidated'] = 6
        cohorts = load_outputs(attribution, source, regions)
        report['historicalOutputCapturesRevalidated'] = 49
        for (profile, frames), buffers in cohorts.items():
            restored = components(buffers)
            outputs = dict(total=buffers['NrdSceneColor'][..., :3], diffuse=restored['Diffuse'], specular=restored['Specular'])
            for roi, mask in sorted(regions.items()):
                diag = buffers['NrdCompositionDiagnostics'][mask]
                if not (diag[:, 0] == 1).all() or not (diag[:, 1] == 0).all():
                    raise ValueError('Fixed ROI uses fallback instead of NRD')
                if any(np.count_nonzero(restored[name][mask]) for name in ('Stable', 'Background')):
                    raise ValueError('Unexpected non-lobe energy on admitted ROI')
                for component in CHANNELS:
                    row = assess(outputs[component], references[11, component], references[29, component], mask,
                        component, roi, qualifications[component, roi])
                    report['records'].append(dict(row, profile=profile, frames=frames))
        report['summary'] = summarize(report['records'])
        for profile in PROFILES:
            for frames in FRAMES:
                for roi in sorted(EXPECTED):
                    rows = [r for r in report['records'] if (r['profile'], r['frames'], r['roi']) == (profile, frames, roi)]
                    report['joint'].append(dict(joint_assessment(rows), profile=profile, frames=frames, roi=roi))
        inventory(report['joint'], {(p, f, r) for p in PROFILES for f in FRAMES for r in EXPECTED},
            ('profile', 'frames', 'roi'))
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Refined-comparison artifact changed')
        report['status'] = 'completed-refined-component-comparison'
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'completed-refined-component-comparison'


if __name__ == '__main__':
    raise SystemExit(main())
