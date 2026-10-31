"""Qualify process-local Debug Layer opt-out without changing the PT estimator."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time

import numpy as np

from compare_nrd_component_references import load_pairs, inventory
from isolate_nrd_glossy_bias import load_references
from requalify_nrd_glossy import read_evidence
from validate_inputs import read_buffer
from validate_nrd_glossy_rois import ROOT
from validate_part1 import sha, write_json

COMPONENTS = ('total', 'diffuse', 'specular')
ORDER = ('validation-on', 'validation-off', 'validation-off', 'validation-on')
SPEED_TARGET = 1.5


def identical(first, second):
    if first.shape != second.shape or not np.isfinite(first).all() or not np.isfinite(second).all():
        raise ValueError('Invalid equivalence images')
    a, b = first.astype(np.float64), second.astype(np.float64)
    if not np.array_equal(a.view(np.uint64), b.view(np.uint64)):
        raise ValueError('Reference radiance is not bit-identical')
    return dict(bitIdentical=True, maximumAbsoluteDifference=0.0)


def speed_result(rows):
    inventory(rows, {(i, mode) for i, mode in enumerate(ORDER)}, ('order', 'mode'))
    if any(not isinstance(r.get('elapsedSeconds'), (int, float)) or not np.isfinite(r['elapsedSeconds']) or
            r['elapsedSeconds'] <= 0 for r in rows):
        raise ValueError('Missing timing evidence')
    elapsed = {mode: statistics.median(r['elapsedSeconds'] for r in rows if r['mode'] == mode)
        for mode in set(ORDER)}
    ratio = elapsed['validation-on']/elapsed['validation-off']
    return dict(medianElapsedSeconds=elapsed, speedup=ratio, minimumUsefulSpeedup=SPEED_TARGET,
        materiallyFaster=ratio >= SPEED_TARGET, includesStartupAndCapture=True)


def capture(exe, folder, label, component, samples, disabled, target):
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    cmd = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'render-preset.json'),
        '-EnablePathTracing']
    if component != 'total':
        cmd += ['-PathTracingReferenceComponent', component]
    if disabled:
        cmd += ['-DisableD3D12DebugLayer']
    cmd += ['-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path), '-PathTracingSamples',
        str(samples), '-LogToFile', str(log), '-LogFPS', '256', '-ExitAfterCapture', '-DisableVSync']
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    print('starting', label, samples, flush=True)
    start = time.perf_counter()
    child = subprocess.Popen(cmd, cwd=ROOT, startupinfo=startup)
    try:
        code = child.wait(timeout=240)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    elapsed = time.perf_counter()-start
    if code != 0 or not path.is_file() or not log.is_file():
        raise ValueError('Missing successful pilot capture: '+label)
    text = log.read_text(encoding='utf-8-sig')
    runtime = [json.loads(line[len('[PathTracing] '):]) for line in text.splitlines() if line.startswith('[PathTracing] ')]
    if len(runtime) != 1 or text.count('[Capture] complete=true') != 1:
        raise ValueError('Missing unique pilot completion evidence')
    if any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
        raise ValueError('Error logged during pilot')
    runtime = runtime[0]
    expected = dict(targetSamples=samples, accumulatedSamples=samples, samplesPerFrame=1, randomSeed=11,
        maxBounces=4, environmentEnabled=True, environmentSamplingMode=7, directLightingEnabled=False,
        emissiveEnabled=False, russianRoulette=False, nrdPreviewRequested=False, nrdFailureCount=0,
        denoiserDistanceDiagnostics=False, d3d12DebugLayerRequested=not disabled, d3d12DebugLayerEnabledByApp=not disabled)
    if any(runtime.get(key) != value for key, value in expected.items()):
        raise ValueError('Pilot runtime or validation state differs')
    meta, image = read_buffer(path)
    if (meta.get('resource') != 'PathTracing.SceneColor' or meta.get('format') != 10 or
            meta.get('sampleStartIndex') != samples or meta.get('samplesPerFrame') != 1 or
            meta.get('randomSeed') != 11 or meta.get('primarySampling') != 'pixel-center' or
            meta.get('denoiserDistanceDiagnostics') is not False or meta.get('nrdPreviewRequested') is not False or
            meta.get('nrdPresentation') != 'Native PT' or
            meta.get('pathTracingReferenceComponent') != (None if component == 'total' else component) or
            (image < 0).any() or not (image[..., 3] == 1).all()):
        raise ValueError('Pilot image identity differs')
    for key in ('width', 'height', 'viewProjection', 'previousViewProjection', 'inverseViewProjection',
            'rayTMin', 'rayTMax', 'normalBias', 'lensShift'):
        if meta.get(key) != target[key]:
            raise ValueError('Pilot camera changed: '+key)
    record = dict(label=label, component=component, samples=samples, validationEnabledByApp=not disabled,
        command=cmd, elapsedSeconds=elapsed, sha256=sha(path), logSha256=sha(log), metadata=meta, diagnostics=runtime,
        errorsObservedInLog=0, d3d12Validation='not-checked-disabled' if disabled else 'enabled-no-errors-observed')
    return image.astype(np.float32), record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'source', 'executable', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    pairs, source, exe = (p.resolve(strict=True) for p in (args.pairs, args.source, args.executable))
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (pairs, source, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be separate and new')
    output.mkdir(parents=True, exist_ok=True)
    paths = (pairs/'report.json', source/'report.json', exe, Path(__file__),
        Path(__file__).with_name('compare_nrd_component_references.py'))
    artifacts = {str(p): sha(p) for p in paths}
    report = dict(schemaVersion=1, status='running', artifactSha256=artifacts, records=[], equivalence=[],
        failures=[], benchmark=[], fullIntegrationReady=False, referenceEstimatorChanged=False,
        limitation='Seed-11 static process-validation opt-out pilot only; no high-sample convergence, NRD quality '
        'or D3D12 validation claim for disabled runs. Debug Layer defaults are unchanged.')
    write_json(output/'report.json', report)
    try:
        _, _, _, target, regions, _, _ = load_references(source)
        load_pairs(pairs, regions, target, source)
        previous = json.loads((pairs/'report.json').read_text(encoding='utf-8'))
        for name in ('scene.json', 'render-preset.json'):
            value = json.loads((pairs/'replay'/name).read_text(encoding='utf-8'))
            write_json(output/name, value)
        fixtures = {str(output/name): sha(output/name) for name in ('scene.json', 'render-preset.json')}
        old_shader = next(Path(p) for p in previous['artifactSha256'] if p.endswith('shaders_PathTracing_CSMain.cso'))
        candidate_shader = exe.parent/'shaders_PathTracing_CSMain.cso'
        if sha(old_shader) != sha(candidate_shader):
            raise ValueError('Runtime PT shader bytecode differs from reviewed reference')
        shaders = sorted(old_shader.parent.glob('*.cso'))
        if not shaders:
            raise ValueError('Missing reviewed shader deployment')
        report['reviewedShaderDeployment'] = []
        for reviewed in shaders:
            deployed = exe.parent/reviewed.name
            digest = sha(reviewed)
            if sha(deployed) != digest:
                raise ValueError('Runtime shader differs: '+reviewed.name)
            artifacts[str(reviewed)] = digest
            artifacts[str(deployed)] = digest
            report['reviewedShaderDeployment'].append(dict(source=str(reviewed), runtime=str(deployed), sha256=digest))
        for component in COMPONENTS:
            old = next(r for r in previous['records'] if r['samples'] == 256 and r['component'] == component)
            _, baseline, _ = read_evidence(pairs/'replay', component, old)
            for disabled in (False, True):
                label = component+('-off' if disabled else '-on')
                image, record = capture(exe, output, label, component, 256, disabled, target)
                report['records'].append(record)
                report['equivalence'].append(dict(identical(baseline, image), label=label,
                    comparator='reviewed-256-sample-replay'))
                write_json(output/'report.json', report)
        baseline = None
        for index, mode in enumerate(ORDER):
            image, record = capture(exe, output, 'benchmark-'+str(index), 'diffuse', 2048,
                mode == 'validation-off', target)
            if baseline is None:
                baseline = image
            report['records'].append(record)
            report['equivalence'].append(dict(identical(baseline, image), label=record['label'],
                comparator='2048-sample-normal-Debug-control'))
            report['benchmark'].append(dict(order=index, mode=mode, elapsedSeconds=record['elapsedSeconds']))
            write_json(output/'report.json', report)
        inventory(report['records'], {(c+suffix,) for c in COMPONENTS for suffix in ('-on', '-off')} |
            {('benchmark-'+str(i),) for i in range(4)}, ('label',))
        report['timing'] = speed_result(report['benchmark'])
        if any(sha(Path(p)) != digest for p, digest in {**artifacts, **fixtures}.items()):
            raise ValueError('Pilot artifacts/fixtures changed')
        report.update(status='passed-fast-reference-pilot' if report['timing']['materiallyFaster'] else 'completed-no-material-speedup',
            fixtureSha256=fixtures)
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] == 'failed-execution'


if __name__ == '__main__':
    raise SystemExit(main())
