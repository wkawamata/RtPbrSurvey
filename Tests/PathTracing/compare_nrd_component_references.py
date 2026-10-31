"""Compare revalidated seed-7 NRD lobes only where frozen component references qualify."""
import argparse
import json
from pathlib import Path

import numpy as np

from attribute_nrd_prepass_lobes import components, attribute
from isolate_nrd_glossy_bias import load_references, check_capture, RESOURCES, LONG_RESOURCES, ROI, arithmetic
from qualify_nrd_component_references import qualify
from requalify_nrd_glossy import read_evidence
from validate_nrd_prepass_sensitivity import load_bridge, command, check_profile
from validate_nrd_glossy_rois import ROOT, EXPECTED
from validate_nrd_reference_components import closure, COMPONENTS
from validate_part1 import sha, write_json

PAIR_SHA = '566efb7d18b2cc58e3836f2c6a95bf6c8b3900c20363687cb9b4e444f02a737c'
ATTRIBUTION_SHA = '386e591ef33ec7c0424b74e2bc49482f9f54759cc1a95eeede8f14212498eac5'
CHANNELS = ('total', 'diffuse', 'specular')
PROFILES = ('control', 'diffuse-5')
FRAMES = (30, 120)
BIAS_LIMIT = .05


def inventory(rows, expected, fields):
    keys = [tuple(row.get(field) for field in fields) for row in rows]
    if len(keys) != len(expected) or set(keys) != expected:
        raise ValueError('Incomplete, duplicate or unexpected component inventory')


def assess(output, first, second, mask, component, roi, qualification):
    if qualification != qualify(first, second, mask, component, roi):
        raise ValueError('Reference qualification changed')
    if (output.shape != first.shape[:2]+(3,) or not np.isfinite(output).all() or (output < 0).any()):
        raise ValueError('Malformed restored component output')
    base = dict(component=component, roi=roi, pixels=int(mask.sum()),
        referenceStatus=qualification['status'], referencePairNrmse=qualification['referencePairNrmse'])
    if not qualification['qualified']:
        return dict(base, status='excluded-unqualified-reference', passed=None)
    actual = output[mask].astype(float)
    if qualification['status'] == 'exact-zero-inactive':
        zero = bool(np.count_nonzero(actual) == 0)
        return dict(base, status='inactive-zero-verified' if zero else 'inactive-energy-leak', passed=zero,
            maximumAbsoluteOutput=float(abs(actual).max()))
    reference = (first[mask, :3].astype(float)+second[mask, :3].astype(float))*.5
    mean = reference.mean(axis=0)
    norm = float(np.linalg.norm(mean))
    if norm <= 1e-6:
        return dict(base, status='excluded-dark-mean', passed=None)
    delta = (actual-reference).mean(axis=0)
    bias = float(np.linalg.norm(delta)/norm)
    return dict(base, status='within-mean-bias-target' if bias <= BIAS_LIMIT else 'outside-mean-bias-target',
        passed=bias <= BIAS_LIMIT, meanRgbBias=bias, signedMeanRgbError=delta.tolist(),
        referenceMeanRgb=mean.tolist(), outputMeanRgb=actual.mean(axis=0).tolist(),
        outputNrmse=float(np.sqrt(np.mean((actual-reference)**2))/qualification['referenceRms']),
        nativeLobeRmseComparisonAvailable=False)


def summarize(rows):
    inventory(rows, {(p, f, c, r) for p in PROFILES for f in FRAMES for c in CHANNELS for r in EXPECTED},
        ('profile', 'frames', 'component', 'roi'))
    for row in rows:
        status = row.get('status')
        if row.get('pixels', 0) < 100:
            raise ValueError('Missing component coverage')
        if status in ('within-mean-bias-target', 'outside-mean-bias-target'):
            vectors = np.asarray([row.get(name, []) for name in
                ('referenceMeanRgb', 'outputMeanRgb', 'signedMeanRgbError')], dtype=float)
            if (vectors.shape != (3, 3) or not np.isfinite(vectors).all() or
                    (vectors[:2] < 0).any() or np.linalg.norm(vectors[0]) <= 1e-6):
                raise ValueError('Missing mean-bias evidence')
            bias = float(np.linalg.norm(vectors[1]-vectors[0])/np.linalg.norm(vectors[0]))
            if (row.get('referenceStatus') != 'qualified-pair' or
                    not isinstance(row.get('referencePairNrmse'), (int, float)) or
                    not 0 <= row['referencePairNrmse'] <= .03 or
                    not np.isclose(row.get('meanRgbBias', np.nan), bias, rtol=1e-10, atol=1e-12) or
                    not np.allclose(vectors[2], vectors[1]-vectors[0], rtol=1e-10, atol=1e-12) or
                    not np.isfinite(row.get('outputNrmse', np.nan)) or row['outputNrmse'] < 0 or
                    row.get('passed') is not (bias <= BIAS_LIMIT) or
                    (status == 'within-mean-bias-target') != (bias <= BIAS_LIMIT)):
                raise ValueError('Inconsistent component bias assessment')
        elif status in ('inactive-zero-verified', 'inactive-energy-leak'):
            value = row.get('maximumAbsoluteOutput', np.nan)
            if (row.get('referenceStatus') != 'exact-zero-inactive' or
                    row.get('referencePairNrmse') is not None or not np.isfinite(value) or value < 0 or
                    row.get('passed') is not (value == 0) or (status == 'inactive-zero-verified') != (value == 0)):
                raise ValueError('Inconsistent inactive component assessment')
        elif status == 'excluded-unqualified-reference':
            pair = row.get('referencePairNrmse')
            if (row.get('passed') is not None or row.get('referenceStatus') not in
                    ('inconclusive-reference', 'inconclusive-dark') or
                    (row['referenceStatus'] == 'inconclusive-reference' and
                    (not isinstance(pair, (int, float)) or not np.isfinite(pair) or pair <= .03))):
                raise ValueError('Unqualified reference was not explicitly excluded')
        elif status != 'excluded-dark-mean' or row.get('passed') is not None or row.get('referenceStatus') != 'qualified-pair':
            raise ValueError('Missing component assessment')
    summary = []
    for profile in PROFILES:
        for frames in FRAMES:
            for component in CHANNELS:
                selected = [r for r in rows if (r['profile'], r['frames'], r['component']) ==
                    (profile, frames, component)]
                summary.append(dict(profile=profile, frames=frames, component=component,
                    withinBiasTarget=sum(r['status'] == 'within-mean-bias-target' for r in selected),
                    outsideBiasTarget=sum(r['status'] == 'outside-mean-bias-target' for r in selected),
                    inactiveZero=sum(r['status'] == 'inactive-zero-verified' for r in selected),
                    inactiveLeak=sum(r['status'] == 'inactive-energy-leak' for r in selected),
                    excluded=sum(r['passed'] is None for r in selected)))
    return summary


def joint_assessment(rows):
    inventory(rows, {(name,) for name in CHANNELS}, ('component',))
    if any(row['status'].startswith('excluded-') for row in rows):
        return dict(status='excluded-unqualified-component', qualityVerdict='not-assessed')
    if any(row['status'] == 'inactive-energy-leak' for row in rows):
        return dict(status='excluded-inactive-energy-leak', qualityVerdict='not-assessed')
    references, errors = {}, {}
    for row in rows:
        component = row['component']
        inactive = row['status'] == 'inactive-zero-verified'
        references[component] = np.zeros(3) if inactive else np.asarray(row['referenceMeanRgb'], dtype=float)
        errors[component] = np.zeros(3) if inactive else np.asarray(row['signedMeanRgbError'], dtype=float)
    luma = np.asarray([.2126, .7152, .0722])
    total_luma = float(references['total'] @ luma)
    if total_luma <= 1e-6:
        return dict(status='excluded-dark-total', qualityVerdict='not-assessed')
    combined = errors['diffuse']+errors['specular']
    separate = float(np.linalg.norm(errors['diffuse'])+np.linalg.norm(errors['specular']))
    return dict(status='qualified-joint-description', qualityVerdict='descriptive-only',
        diffuseReferenceLuminanceFraction=float(references['diffuse'] @ luma/total_luma),
        specularReferenceLuminanceFraction=float(references['specular'] @ luma/total_luma),
        lobeErrorCancellationRatio=float(np.linalg.norm(combined)/separate) if separate > 0 else None,
        lobeErrorSumRgb=combined.tolist(), totalMeanErrorRgb=errors['total'].tolist(),
        referenceMeanClosureResidualRgb=(references['total']-references['diffuse']-references['specular']).tolist(),
        errorClosureResidualRgb=(errors['total']-combined).tolist())


def load_pairs(folder, regions, target, source):
    if sha(folder/'report.json') != PAIR_SHA:
        raise ValueError('Expected frozen, reviewed paired-reference report')
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    if (report['status'] != 'completed-reference-pair-study' or report['failures'] or
            report['samples'] != 8192 or report['samplesPerFrame'] != 1 or report['pairNrmseLimit'] != .03 or
            report['masksReselected'] is not False or report['fullIntegrationReady'] is not False or
            report['artifactSha256'].get(str(source/'report.json')) != sha(source/'report.json')):
        raise ValueError('Invalid paired-reference policy')
    for path, digest in {**report['artifactSha256'], **report['fixtureSha256']}.items():
        if sha(Path(path)) != digest:
            raise ValueError('Paired-reference artifact/fixture changed: '+path)
    expected = {(11, 256, name) for name in ('total', *COMPONENTS)} | {
        (seed, 8192, name) for seed in (11, 29) for name in CHANNELS}
    inventory(report['records'], expected, ('seed', 'samples', 'component'))
    inventory(report['rois'], {(c, r) for c in CHANNELS for r in EXPECTED}, ('component', 'roi'))
    references, replay = {}, {}
    for record in report['records']:
        seed, samples, name = record['seed'], record['samples'], record['component']
        capture_folder = folder/('replay' if samples == 256 else str(seed))
        if Path(record['evidenceFolder']) != capture_folder or record['label'] != name:
            raise ValueError('Reference record identity changed')
        meta, image, runtime = read_evidence(capture_folder, name, record)
        if meta != record['metadata'] or runtime['accumulatedSamples'] != samples:
            raise ValueError('Reference metadata changed')
        for key in ('width', 'height', 'viewProjection', 'previousViewProjection', 'inverseViewProjection',
                'rayTMin', 'rayTMax', 'normalBias', 'lensShift'):
            if meta[key] != target[key]:
                raise ValueError('Reference camera/geometry changed: '+key)
        if samples == 256:
            replay[name] = image
        else:
            references[seed, name] = image
    if closure(replay) != report['groupingClosure']:
        raise ValueError('Reference energy closure changed')
    qualifications = {(r['component'], r['roi']): r for r in report['rois']}
    for component in CHANNELS:
        for roi, mask in regions.items():
            if qualify(references[11, component], references[29, component], mask, component, roi) != qualifications[component, roi]:
                raise ValueError('Reference variance assessment changed')
    return references, qualifications


def load_outputs(folder, source, regions):
    if sha(folder/'report.json') != ATTRIBUTION_SHA:
        raise ValueError('Expected frozen seed-7 attribution report')
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    if report['status'] != 'completed-lobe-sensitivity-attribution' or report['failures']:
        raise ValueError('Invalid lobe attribution report')
    for path, digest in report['artifactSha256'].items():
        if sha(Path(path)) != digest:
            raise ValueError('Attribution artifact changed: '+path)
    bridge = Path(report['bridgeEvidence'])
    bridge_report = json.loads((bridge/'report.json').read_text(encoding='utf-8'))
    if Path(bridge_report['referenceEvidence']) != source:
        raise ValueError('Attribution/reference chains differ')
    exe = Path(bridge_report['executable'])
    scene, preset, target, _, _, baseline = load_bridge(bridge, exe)
    control_folder = Path(report['controlEvidence'])
    control = json.loads((control_folder/'report.json').read_text(encoding='utf-8'))
    if (control['status'] != 'completed-diagnostic' or control['failures'] or
            control['sourceReportSha256'] != sha(source/'report.json') or Path(control['sourceEvidence']) != source):
        raise ValueError('Invalid diagnostic control report')
    for path, digest in control['artifactSha256'].items():
        if sha(ROOT/path) != digest:
            raise ValueError('Control artifact changed: '+path)
    for evidence, manifest in ((control_folder, control), (folder, report)):
        for name, value in (('scene.json', scene), ('render-preset.json', preset)):
            if (sha(evidence/name) != manifest['fixtureSha256'][name] or
                    json.loads((evidence/name).read_text(encoding='utf-8')) != value):
                raise ValueError('Output fixture changed')
    inventory(control['records'], {(f, n) for f, names in ((30, RESOURCES), (120, LONG_RESOURCES)) for n in names},
        ('frames', 'resource'))
    inventory(report['records'], {(f, n) for f in FRAMES for n in LONG_RESOURCES}, ('frames', 'resource'))
    cohorts = {}
    for profile, manifest in (('control', control), ('diffuse-5', report)):
        for frames in FRAMES:
            buffers, common = {}, None
            for record in manifest['records']:
                if record['frames'] != frames:
                    continue
                name, label = record['resource'], record['label']
                evidence = control_folder if profile == 'control' else Path(record['evidenceFolder'])
                for filename, value in (('scene.json', scene), ('render-preset.json', preset)):
                    if json.loads((evidence/filename).read_text(encoding='utf-8')) != value:
                        raise ValueError('Capture-folder fixture changed')
                if profile == 'control':
                    expected_command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(evidence/'scene.json'),
                        '-RenderPreset', str(evidence/'render-preset.json'), '-EnablePathTracing', '-DebugPreviewResource',
                        'PathTracing.'+name, '-CapturePath', str(evidence/(label+'.ptbuf')), '-CaptureAfterFrames', str(frames),
                        '-LogToFile', str(evidence/(label+'.log')), '-ExitAfterCapture', '-DisableVSync']
                else:
                    expected_command = command(exe, evidence, 7, frames, name, profile, label)
                if record['command'] != expected_command:
                    raise ValueError('Output acquisition command changed')
                meta, image, runtime = read_evidence(evidence, label, record)
                if meta != record['metadata']:
                    raise ValueError('Output metadata changed')
                common = check_capture(meta, runtime, name, frames, target, common)
                if profile != 'control':
                    check_profile(meta, profile)
                buffers[name] = image
            if profile == 'control':
                for name in ('SceneColor', 'NrdSceneColor'):
                    if not np.array_equal(buffers[name], baseline[frames, name][0]):
                        raise ValueError('Control image differs from verified bridge')
                if frames == 30 and arithmetic(buffers) != control['arithmetic']:
                    raise ValueError('Control split/admission arithmetic changed')
            cohorts[profile, frames] = buffers
    for frames in FRAMES:
        old = next(r for r in report['checkpoints'] if r['frames'] == frames)
        result = attribute(cohorts['control', frames], cohorts['diffuse-5', frames], regions[ROI])
        if any(old.get(key) != value for key, value in result.items()):
            raise ValueError('Historical attribution assessment changed')
    return cohorts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'attribution', 'references', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    pairs, attribution, source = (p.resolve(strict=True) for p in (args.pairs, args.attribution, args.references))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (pairs, attribution, source)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a separate new folder')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {str(p): sha(p) for p in (pairs/'report.json', attribution/'report.json', source/'report.json',
        Path(__file__), Path(__file__).with_name('attribute_nrd_prepass_lobes.py'),
        Path(__file__).with_name('qualify_nrd_component_references.py'),
        Path(__file__).with_name('validate_nrd_composition.py'))}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, records=[], summary=[], joint=[], failures=[],
        seed=7, freshGpuCaptures=0, historicalGpuCapturesRevalidated=0, referenceCapturesRevalidated=0,
        meanRgbBiasTarget=BIAS_LIMIT, fullIntegrationReady=False, qualityVerdict='not-promoted',
        subjectiveVerdict='not-repeated', limitation='Static single-seed component mean-bias study, not an SDK '
        'quality guarantee or a Native-vs-NRD lobe RMSE comparison. NRD history cap is 30, not 120 samples.')
    try:
        _, _, _, target, regions, _, _ = load_references(source)
        references, qualifications = load_pairs(pairs, regions, target, source)
        report['referenceCapturesRevalidated'] = 11
        cohorts = load_outputs(attribution, source, regions)
        report['historicalGpuCapturesRevalidated'] = 49
        for (profile, frames), buffers in cohorts.items():
            restored = components(buffers)
            outputs = dict(total=buffers['NrdSceneColor'][..., :3], diffuse=restored['Diffuse'], specular=restored['Specular'])
            for roi, mask in sorted(regions.items()):
                diag = buffers['NrdCompositionDiagnostics'][mask]
                if not (diag[:, 0] == 1).all() or not (diag[:, 1] == 0).all():
                    raise ValueError('Fixed ROI contains fallback instead of NRD')
                for component in CHANNELS:
                    row = assess(outputs[component], references[11, component], references[29, component],
                        mask, component, roi, qualifications[component, roi])
                    report['records'].append(dict(row, profile=profile, frames=frames))
                if any(np.count_nonzero(restored[name][mask]) for name in ('Stable', 'Background')):
                    raise ValueError('Joint lobe study requires zero stable/background on admitted surface ROIs')
        report['summary'] = summarize(report['records'])
        for profile in PROFILES:
            for frames in FRAMES:
                for roi in sorted(EXPECTED):
                    rows = [r for r in report['records'] if (r['profile'], r['frames'], r['roi']) == (profile, frames, roi)]
                    report['joint'].append(dict(joint_assessment(rows), profile=profile, frames=frames, roi=roi))
        inventory(report['joint'], {(p, f, r) for p in PROFILES for f in FRAMES for r in EXPECTED},
            ('profile', 'frames', 'roi'))
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Comparison artifact changed')
        report['status'] = 'completed-qualified-component-comparison'
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'completed-qualified-component-comparison'


if __name__ == '__main__':
    raise SystemExit(main())
