"""Assess continuous reversal/settling captures against fresh independent Native references."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer
from validate_nrd_disocclusion import (ROOT, FIXTURE, RESOURCES, check_cohort,
    assess_visibility, visibility_regions)
from validate_nrd_camera_motion import assess_motion
from validate_nrd_timeline import CHECKPOINTS, make_plan, check_scheduled, check_frame
from validate_nrd_disocclusion_quality import (SAMPLES, SEEDS, LIMITS, assess_region,
    capture_reference, check_reference)
from validate_part1 import sha, write_json

GROUPS = (('zero', ('warm', 'middle')), ('positive16', ('forward', 'reverse')),
    ('negative20', ('stop', 'settle-4', 'settle-16', 'settle-32')))
UNCERTAINTY_MULTIPLIER = 1.0


def validate_inventory(report):
    expected = {(case, frame, resource) for case, frame, _, _ in CHECKPOINTS for resource in RESOURCES}
    records = report.get('records', [])
    observed = [(record.get('case'), record.get('frame'), record.get('resource')) for record in records]
    cases = [(entry.get('case'), entry.get('frame')) for entry in report.get('cases', [])]
    runs = [entry.get('resource') for entry in report.get('runs', [])]
    if (report.get('status') != 'passed' or report.get('failures') != [] or
            len(observed) != len(expected) or set(observed) != expected or
            len(cases) != len(CHECKPOINTS) or set(cases) != {(case, frame) for case, frame, _, _ in CHECKPOINTS} or
            len(runs) != len(RESOURCES) or set(runs) != set(RESOURCES)):
        raise ValueError('Incomplete, duplicate or failed timeline source')


def load_source(folder):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    validate_inventory(report)
    if (report['executableSha256'] != sha(ROOT/'build/Debug/RtPbrSurvey.exe') or
            report['scriptSha256'] != sha(ROOT/'Tests/PathTracing/validate_nrd_timeline.py')):
        raise ValueError('Timeline executable/runner differs from current artifacts')
    expected_maps = (
        ('fixtureSha256', FIXTURE, {'scene.json', 'render-preset.json'}),
        ('helperSha256', Path(__file__).parent, {'validate_nrd_disocclusion.py',
            'validate_nrd_camera_motion.py', 'validate_secondary_geometry.py', 'validate_inputs.py'}),
        ('shaderSha256', ROOT/'build/shaders', {'shaders_PathTracing_CSMain.cso',
            'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso'}))
    for field, directory, names in expected_maps:
        if set(report.get(field, {})) != names:
            raise ValueError('Incomplete timeline provenance: '+field)
        if any(sha(directory/name) != digest for name, digest in report[field].items()):
            raise ValueError('Changed timeline artifact: '+field)
    manifest = ROOT/'build/shaders/nrd-relax-manifest.txt'
    identifiers = manifest.read_text().splitlines()
    if (report.get('shaderManifestSha256') != sha(manifest) or
            set(report.get('sdkShaderSha256', {})) != set(identifiers) or
            any(report['sdkShaderSha256'][name] != sha(ROOT/f'build/shaders/nrd-relax-{index}.cso')
                for index, name in enumerate(identifiers))):
        raise ValueError('Incomplete or changed SDK shader provenance')
    for run in report['runs']:
        directory = folder/run['resource']
        log, plan = directory/'runtime.log', directory/'plan.json'
        if sha(log) != run['logSha256'] or sha(plan) != run['planSha256']:
            raise ValueError('Timeline plan/log hash changed')
        if json.loads(plan.read_text(encoding='utf-8')) != make_plan():
            raise ValueError('Unexpected timeline schedule')
        text = log.read_text(encoding='utf-8-sig')
        if any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
            raise ValueError('Timeline runtime error')
        scheduled = [json.loads(line[len('[PathTracingTimeline] '):]) for line in text.splitlines()
            if line.startswith('[PathTracingTimeline] ')]
        check_scheduled(scheduled, directory)
        completed = [line for line in text.splitlines() if line.startswith('[Capture] ')]
        if (len(completed) != len(CHECKPOINTS) or 'complete=true' not in completed[-1] or
                any('complete=false' not in line for line in completed[:-1])):
            raise ValueError('Incomplete timeline capture drain')
    scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))
    records = {(record['case'], record['resource']): record for record in report['records']}
    outputs, cameras, motion_x = {}, {}, {}
    for case, frame, moving, sample_index in CHECKPOINTS:
        common, buffers, metadata, paths = None, {}, {}, {}
        for resource in RESOURCES:
            path = folder/resource/(case+'-pt.ptbuf')
            record = records[case, resource]
            if Path(record['path']).resolve() != path or sha(path) != record['sha256']:
                raise ValueError('Changed timeline capture: '+case+' '+resource)
            meta, image = read_buffer(path)
            common = check_cohort(meta, resource, common)
            check_frame(meta, frame, moving, sample_index)
            buffers[resource], metadata[resource], paths[resource] = image.astype(np.float32), meta, path
        meta = metadata['Albedo']
        assess_visibility(meta, buffers['Albedo'], scene, moving)
        motion = assess_motion(metadata['MotionVectors'], buffers['MotionVectors'], scene, moving)
        cameras[case], motion_x[case] = np.asarray(meta['viewProjection']), motion['observedUvMean'][0]
        for resource in ('NrdSceneColor', 'SceneColor'):
            image = buffers[resource]
            if (image < 0).any() or (image[..., 3] != 1).any():
                raise ValueError('Invalid timeline radiance/alpha')
        outputs[case] = (meta, paths['NrdSceneColor'], paths['SceneColor'])
    if (not np.allclose(cameras['forward'], cameras['reverse'], atol=2e-6, rtol=2e-6) or
            motion_x['forward']*motion_x['reverse'] >= 0 or any(
                not np.allclose(cameras[case], cameras['stop'], atol=2e-6, rtol=2e-6)
                for case in ('settle-4', 'settle-16', 'settle-32'))):
        raise ValueError('Timeline reversal/settling camera relationship changed')
    return scene, outputs


def assess_stability(stop, settled):
    if set(stop) != {'revealed', 'retained'} or set(settled) != set(stop):
        raise ValueError('Missing stop/final settling region')
    results = {}
    for name in stop:
        first, last = stop[name], settled[name]
        if (first['sampledPixels'] != last['sampledPixels'] or
                first['referenceMeanRgb'] != last['referenceMeanRgb'] or
                first['referencePairNrmse'] != last['referencePairNrmse']):
            raise ValueError('Stop/final reference or region differs')
        budget = UNCERTAINTY_MULTIPLIER*first['referencePairNrmse']
        increase = last['nrdNrmse']-first['nrdNrmse']
        results[name] = dict(passed=increase <= budget, stopNrmse=first['nrdNrmse'],
            finalNrmse=last['nrdNrmse'], increase=increase, referenceUncertaintyBudget=budget)
    return results


def validate_region_result(value, minimum):
    fields = ('referencePairNrmse', 'nrdNrmse', 'nativeNrmse', 'meanRgbBias',
        'meanRedExcess', 'p95RedExcess')
    if (type(value.get('sampledPixels')) is not int or value['sampledPixels'] < minimum or any(
            type(value.get(field)) not in (int, float) or not np.isfinite(value[field]) or value[field] < 0
            for field in fields)):
        raise ValueError('Missing or invalid quality measurements')
    for field in ('referenceMeanRgb', 'nrdMeanRgb'):
        rgb = np.asarray(value.get(field), dtype=float)
        if rgb.shape != (3,) or not np.isfinite(rgb).all() or (rgb < 0).any():
            raise ValueError('Missing or invalid quality RGB means')
    expected = dict(referenceConverged=value['referencePairNrmse'] <= LIMITS['referencePairNrmse'],
        noRmseRegression=value['nrdNrmse'] <= value['nativeNrmse']*LIMITS['nrdToNativeRmse'],
        meanRgbBias=value['meanRgbBias'] <= LIMITS['meanRgbBias'],
        meanRedExcess=value['meanRedExcess'] <= LIMITS['meanRedExcess'],
        p95RedExcess=value['p95RedExcess'] <= LIMITS['p95RedExcess'])
    if value.get('checks') != expected or value.get('passed') is not all(expected.values()):
        raise ValueError('Quality flags disagree with required numeric criteria')


def quality_verdict(cases):
    names = [entry.get('case') for entry in cases]
    if len(names) != len(CHECKPOINTS) or set(names) != {case for case, _, _, _ in CHECKPOINTS}:
        raise ValueError('Incomplete or duplicate quality checkpoints')
    for entry in cases:
        expected = {'retained'} if entry['case'] == 'warm' else {'revealed', 'retained'}
        if set(entry.get('regions', {})) != expected:
            raise ValueError('Incomplete or unexpected quality regions')
        for name, value in entry['regions'].items():
            validate_region_result(value, 100 if name == 'retained' else 20)
    regions = [value for entry in cases for value in entry['regions'].values()]
    if not all(value['checks']['referenceConverged'] for value in regions):
        return 'inconclusive-reference', {}
    indexed = {entry['case']: entry['regions'] for entry in cases}
    stability = assess_stability(indexed['stop'], indexed['settle-32'])
    passed = all(value['passed'] for value in regions) and all(value['passed'] for value in stability.values())
    return 'passed-timeline-fixture' if passed else 'failed-timeline-fixture', stability


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, folder = args.source.resolve(strict=True), args.output.resolve()
    if folder == source or source in folder.parents or folder == FIXTURE or FIXTURE in folder.parents or (
            folder.exists() and any(folder.iterdir())):
        parser.error('Output must be a separate empty directory')
    report = dict(status='running', qualityVerdict='not-evaluated', fullIntegrationReady=False,
        sourceEvidence=str(source), sourceReportSha256=sha(source/'report.json'), sourceEvidenceReexecuted=False,
        executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'), scriptSha256=sha(Path(__file__)),
        helperSha256={name: sha(Path(__file__).parent/name) for name in
            ('validate_nrd_timeline.py', 'validate_nrd_disocclusion_quality.py')},
        freshReferenceSamples=SAMPLES, referenceSeeds=SEEDS, limits=LIMITS,
        stabilityReferenceUncertaintyMultiplier=UNCERTAINTY_MULTIPLIER, records=[], cases=[], failures=[])
    write_json(folder/'report.json', report)
    try:
        scene, outputs = load_source(source)
        report['sourceEvidenceRevalidated'] = True
        stop_regions = visibility_regions(outputs['stop'][0], scene)
        for group, cases in GROUPS:
            target = outputs[cases[0]][0]
            references, reference_records = [], []
            for seed in SEEDS:
                image, record = capture_reference(folder, scene, target, seed, group+'-reference-'+str(seed))
                references.append(image)
                reference_records.append(record)
                report['records'].append(record)
                write_json(folder/'report.json', report)
                print('captured '+record['label'], flush=True)
            for case in cases:
                meta, nrd_path, native_path = outputs[case]
                for record in reference_records:
                    check_reference(record['metadata'], record['diagnostics'], meta, record['diagnostics']['randomSeed'])
                _, nrd = read_buffer(nrd_path)
                _, native = read_buffer(native_path)
                regions = stop_regions if case.startswith('settle-') else visibility_regions(meta, scene)
                xs, ys, _, _, revealed, retained = regions
                entry = dict(case=case, frame=meta['nrdLastEvaluation']['frameIndex'],
                    nativeSampleIndex=meta['sampleStartIndex'], referenceGroup=group,
                    regionBasis='stop-frame-80' if case.startswith('settle-') else 'current-frame', regions={})
                for name, mask, minimum in (('revealed', revealed, 20), ('retained', retained, 100)):
                    if case == 'warm' and name == 'revealed':
                        if mask.any():
                            raise ValueError('Warm static camera revealed a hidden surface')
                        continue
                    if mask.sum() < minimum:
                        raise ValueError('Insufficient quality region: '+case+' '+name)
                    values = [image[ys[mask], xs[mask], :3] for image in (nrd, native, *references)]
                    entry['regions'][name] = assess_region(*values)
                report['cases'].append(entry)
                write_json(folder/'report.json', report)
        order = {case: index for index, (case, _, _, _) in enumerate(CHECKPOINTS)}
        report['cases'].sort(key=lambda entry: order[entry['case']])
        verdict, stability = quality_verdict(report['cases'])
        report.update(status='passed' if verdict == 'passed-timeline-fixture' else 'failed',
            qualityVerdict=verdict, settlingStability=stability)
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'One opaque two-plane fixture, eight sparse checkpoints and independent finite high-sample Monte Carlo references. Historical timeline is hash-revalidated, not reexecuted. Numeric fixture gate only; no per-frame flicker, subjective, camera-cut or object-motion acceptance.'
    write_json(folder/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
