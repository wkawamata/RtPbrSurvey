"""Refine physical-lobe references to 32768 samples without weakening qualification."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from compare_nrd_component_references import load_pairs, inventory, CHANNELS
from isolate_nrd_glossy_bias import load_references
from qualify_nrd_component_references import qualify
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_rois import ROOT, EXPECTED
from validate_part1 import sha, write_json

SAMPLES = 32768
SEEDS = (11, 29)
COMPONENTS = ('diffuse', 'specular')


def summarize(rows):
    inventory(rows, {(c, r) for c in CHANNELS for r in EXPECTED}, ('component', 'roi'))
    for row in rows:
        pair, status = row.get('referencePairNrmse'), row.get('status')
        if row.get('pixels', 0) < 100:
            raise ValueError('Missing reference coverage')
        if status in ('qualified-pair', 'inconclusive-reference'):
            passed = status == 'qualified-pair'
            if (not isinstance(pair, (int, float)) or not np.isfinite(pair) or pair < 0 or
                    (pair <= .03) != passed or row.get('qualified') is not passed):
                raise ValueError('Inconsistent reference qualification')
        elif status == 'exact-zero-inactive':
            if (row['component'] != 'diffuse' or not row['roi'].startswith('metal-') or
                    pair is not None or row.get('qualified') is not True or
                    row.get('referenceRms') != 0 or row.get('maximumPairAbsoluteDifference') != 0):
                raise ValueError('Invalid inactive reference')
        elif status != 'inconclusive-dark' or row.get('qualified') is not False or pair is not None:
            raise ValueError('Missing reference assessment')
    return [dict(component=c, qualifiedPairs=sum(r['status'] == 'qualified-pair' for r in rows if r['component'] == c),
        inactiveZero=sum(r['status'] == 'exact-zero-inactive' for r in rows if r['component'] == c),
        inconclusive=sum(not r['qualified'] for r in rows if r['component'] == c)) for c in CHANNELS]


def check_capture(meta, runtime, component, seed, target):
    if (component not in COMPONENTS or seed not in SEEDS or meta.get('resource') != 'PathTracing.SceneColor' or
            meta.get('format') != 10 or meta.get('sampleStartIndex') != SAMPLES or
            meta.get('samplesPerFrame') != 1 or meta.get('randomSeed') != seed or
            meta.get('primarySampling') != 'pixel-center' or meta.get('nrdPreviewRequested') is not False or
            meta.get('nrdPresentation') != 'Native PT' or meta.get('denoiserDistanceDiagnostics') is not False or
            meta.get('pathTracingReferenceComponent') != component):
        raise ValueError('Wrong refined capture identity')
    expected = dict(accumulatedSamples=SAMPLES, samplesPerFrame=1, randomSeed=seed, targetSamples=SAMPLES,
        maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
        emissiveEnabled=False, russianRoulette=False, nrdPreviewRequested=False, nrdFailureCount=0)
    if any(runtime.get(key) != value for key, value in expected.items()):
        raise ValueError('Wrong refined transport state')
    for key in ('width', 'height', 'viewProjection', 'previousViewProjection', 'inverseViewProjection',
            'rayTMin', 'rayTMax', 'normalBias', 'lensShift'):
        if meta.get(key) != target[key]:
            raise ValueError('Refined camera/geometry changed: '+key)


def capture(exe, folder, component, seed, target, hidden=False):
    path, log = folder/(component+'.ptbuf'), folder/(component+'.log')
    command = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'render-preset.json'),
        '-EnablePathTracing', '-PathTracingReferenceComponent', component, '-DebugPreviewResource',
        'PathTracing.SceneColor', '-CapturePath', str(path), '-PathTracingSamples', str(SAMPLES),
        '-LogToFile', str(log), '-LogFPS', '1024', '-ExitAfterCapture', '-DisableVSync']
    print('starting', component, seed, SAMPLES, flush=True)
    launch = {}
    if hidden:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        launch['startupinfo'] = startup
    child = subprocess.Popen(command, cwd=ROOT, **launch)
    try:
        code = child.wait(timeout=3000)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0:
        raise ValueError('Reference capture failed: '+component+'/'+str(seed))
    if not path.is_file() or not log.is_file():
        raise ValueError('Reference process exited with code 0 without complete capture/log evidence: '+component+'/'+str(seed))
    record = dict(label=component, component=component, seed=seed, samples=SAMPLES,
        command=command, evidenceFolder=str(folder), acquisition='fresh',
        sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
    meta, image, runtime = read_evidence(folder, component, record)
    check_capture(meta, runtime, component, seed, target)
    if not np.isfinite(image).all() or (image < 0).any() or not (image[..., 3] == 1).all():
        raise ValueError('Malformed reference image')
    record.update(metadata=meta, diagnostics=runtime)
    return image, record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'source', 'executable', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--hidden', action='store_true', help='Hide capture windows to avoid interactive input/resize.')
    args = parser.parse_args()
    pairs, source, exe = (p.resolve(strict=True) for p in (args.pairs, args.source, args.executable))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (pairs, source, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a separate new folder')
    output.mkdir(parents=True, exist_ok=True)
    paths = (pairs/'report.json', source/'report.json', exe, exe.parent.parent/'shaders/shaders_PathTracing_CSMain.cso',
        Path(__file__), Path(__file__).with_name('compare_nrd_component_references.py'),
        Path(__file__).with_name('qualify_nrd_component_references.py'))
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, fixtureSha256={}, records=[], rois=[],
        summary=[], failures=[], samples=SAMPLES, seeds=list(SEEDS), samplesPerFrame=1, pairNrmseLimit=.03,
        freshGpuCaptures=0, reusedTotalCapturesRevalidated=0, fullIntegrationReady=False, masksReselected=False,
        windowMode='hidden' if args.hidden else 'normal',
        limitation='Reference variance only. Reused historical Total and fresh component builds are explicitly '
        'distinct. No NRD output quality or performance-ceiling verdict.')
    write_json(output/'report.json', report)
    try:
        scene, preset, _, target, regions, total, _ = load_references(source)
        _, previous = load_pairs(pairs, regions, target, source)
        paired_report = json.loads((pairs/'report.json').read_text(encoding='utf-8'))
        if paired_report['artifactSha256'].get(str(exe)) != sha(exe):
            raise ValueError('Reference executable differs from reviewed 8192-sample study')
        original = json.loads((source/'report.json').read_text(encoding='utf-8'))
        references = {}
        for seed, image in zip(SEEDS, total):
            record = next(r for r in original['records'] if r['label'] == 'reference-b4-'+str(seed))
            references[seed, 'total'] = image
            report['records'].append(dict(record, component='total', seed=seed, samples=SAMPLES,
                acquisition='revalidated-total', evidenceFolder=str(source)))
        report['reusedTotalCapturesRevalidated'] = 2
        write_json(output/'report.json', report)
        for seed in SEEDS:
            folder = output/str(seed)
            changed = copy.deepcopy(preset)
            changed['pathTracing'].update(samplesPerFrame=1, nrdPreview=False, denoiserDistanceDiagnostics=False,
                randomSeed=seed, debugOutput=3)
            write_json(folder/'scene.json', scene)
            write_json(folder/'render-preset.json', changed)
            for name in ('scene.json', 'render-preset.json'):
                report['fixtureSha256'][str(folder/name)] = sha(folder/name)
            for component in COMPONENTS:
                report['activeCapture'] = dict(component=component, seed=seed, samples=SAMPLES, evidenceFolder=str(folder))
                write_json(output/'report.json', report)
                image, record = capture(exe, folder, component, seed, target, args.hidden)
                references[seed, component] = image
                report['records'].append(record)
                report['freshGpuCaptures'] += 1
                report['activeCapture'] = None
                write_json(output/'report.json', report)
        inventory(report['records'], {(c, s) for c in CHANNELS for s in SEEDS}, ('component', 'seed'))
        for component in CHANNELS:
            for roi, mask in sorted(regions.items()):
                row = qualify(references[11, component], references[29, component], mask, component, roi)
                old = previous[component, roi]
                report['rois'].append(dict(row, previous8192Status=old['status'],
                    previous8192PairNrmse=old['referencePairNrmse']))
        report['summary'] = summarize(report['rois'])
        if any(sha(Path(p)) != digest for p, digest in {**artifacts, **report['fixtureSha256']}.items()):
            raise ValueError('Refinement artifacts/fixtures changed')
        report.update(status='completed-component-reference-refinement',
            referenceVerdict='qualified-all-components' if all(r['qualified'] for r in report['rois']) else 'partially-inconclusive')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report.get('referenceVerdict'), report['failures'], flush=True)
    return report['status'] != 'completed-component-reference-refinement'


if __name__ == '__main__':
    raise SystemExit(main())
