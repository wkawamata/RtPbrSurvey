"""Qualify pre-recording NRD failure fallback without changing SDK files or GPU drivers."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from validate_history import CASES, validate_records, normalize_accumulation, assess_images
from validate_inputs import read_buffer
from validate_nrd_presentation import assess_pair, RELAX
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FALLBACK = 'Native fallback: NRD backend unavailable'


def assess_failure(record, mode):
    if type(record.get('nrdFailureCount')) is not int or record['nrdFailureCount'] != 1:
        raise ValueError('Missing failure count or disabled backend was retried')
    if record.get('nrdPreviewRequested') is not True or record.get('nrdPresentation') != FALLBACK:
        raise ValueError('Failure did not select explicit Native fallback')
    prefix = 'initialization: ' if mode == 'initialization' else 'preparation: '
    reason = record.get('nrdFailure')
    if not isinstance(reason, str) or not reason.startswith(prefix) or len(reason) <= len(prefix):
        raise ValueError('Missing or wrong-stage failure reason')
    prepared, planned = record.get('nrdPreparedDispatches'), record.get('nrdPlannedDispatches')
    if type(prepared) is not int or type(planned) is not int:
        raise ValueError('Missing preparation counters')
    if mode == 'initialization':
        if prepared != 0 or planned != 0:
            raise ValueError('Initialization failure recorded a dispatch plan')
    elif prepared <= 0 or planned != prepared+1:
        raise ValueError('Late-invalid dispatch was not rejected after preparing all earlier dispatches')
    return dict(reason=reason, preparedDispatches=prepared, plannedDispatches=planned)


def check_log(log, result):
    text = log.read_text(encoding='utf-8-sig')
    if result.returncode or any(token in text for token in ('[ERROR]', '[CORRUPTION]', '[PathTracingHistoryFailure]')):
        raise ValueError('Runtime or D3D12 failure: '+str(log))
    return text.splitlines()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if source == output or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    scene = json.loads((source/'scene.json').read_text())
    scene['materials'][0]['metallic'] = 1.0
    preset = json.loads((source/('preset.json' if (source/'preset.json').exists() else 'on.json')).read_text())
    preset['pathTracing'].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True,
        denoiserDistanceDiagnostics=True, debugOutput=3, nrdPreview=False)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    report = dict(status='running', fullIntegrationReady=False, captures=[], comparisons=[], failures=[],
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'),
        presetSha256=sha(output/'preset.json'),
        backendSourceSha256=sha(ROOT/'Renderer/NrdRelaxD3D12.cpp'),
        compositionShaderSha256=sha(exe.parent/'shaders_NrdComposition_CSMain.cso'))
    write_json(output/'report.json', report)

    def capture(label, samples, mode='', preview=True, resource=''):
        path = output/(label+('.ptbuf' if resource else '.png'))
        log = output/(label+'.log')
        command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
            '-EnablePathTracing', '-PathTracingSamples', str(samples), '-CapturePath', str(path),
            '-LogToFile', str(log), '-ExitAfterCapture']
        if preview:
            command.append('-NrdPreview')
        if mode:
            command += ['-NrdDiagnosticFailure', mode]
        if resource:
            command += ['-DebugPreviewResource', 'PathTracing.'+resource]
        result = subprocess.run(command, cwd=ROOT, timeout=180)
        lines = check_log(log, result)
        records = [json.loads(line[len('[PathTracing] '):]) for line in lines if line.startswith('[PathTracing] ')]
        if len(records) != 1 or records[0].get('accumulatedSamples') != samples:
            raise ValueError('Capture sample count or diagnostics missing: '+label)
        entry = dict(label=label, command=command, path=str(path), sha256=sha(path),
            logSha256=sha(log), diagnostics=records[0], d3d12Errors=0)
        report['captures'].append(entry)
        if mode and (mode != 'after-four-frames' or samples > 4):
            entry['assessment'] = assess_failure(records[0], mode)
        else:
            expected = RELAX if preview else 'Native PT'
            if records[0].get('nrdPresentation') != expected or records[0].get('nrdFailure') != '':
                raise ValueError('Healthy or pre-injection control failed: '+label)
        write_json(output/'report.json', report)
        return read_buffer(path) if resource else np.array(Image.open(path).convert('RGB'))

    try:
        for label, samples, mode in (('initialization', 5, 'initialization'),
                ('early-plan', 1, 'last-dispatch'), ('before-failure', 3, 'after-four-frames'),
                ('failure-frame', 5, 'after-four-frames'), ('disabled-later', 12, 'after-four-frames')):
            native = capture(label+'-native', samples, preview=False)
            actual = capture(label+'-preview', samples, mode)
            comparison = assess_pair(native, actual, qualified=label == 'before-failure')
            report['comparisons'].append(dict(case=label, **comparison))
            write_json(output/'report.json', report)
            print('passed '+label, comparison, flush=True)
        raw = {}
        for resource in ('SceneColor', 'NrdSceneColor', 'NrdCompositionDiagnostics'):
            meta, image = capture('raw-'+resource, 5, 'after-four-frames', resource=resource)
            if (meta.get('resource') != 'PathTracing.'+resource or meta.get('format') != (10 if resource == 'SceneColor' else 2)
                    or meta.get('sampleStartIndex') != 5):
                raise ValueError('Raw fallback capture identity mismatch')
            assess_failure(meta, 'after-four-frames')
            attempt = meta.get('nrdLastEvaluation', {})
            if (attempt.get('attempted') is not True or attempt.get('recorded') is not False or
                    attempt.get('dispatchCount') != 0 or attempt.get('frameIndex') != 4):
                raise ValueError('Failed last attempt was reported as GPU execution')
            raw[resource] = image
        if (not np.array_equal(raw['SceneColor'], raw['NrdSceneColor']) or
                not (raw['NrdCompositionDiagnostics'] == [0, 1, 0, 0]).all()):
            raise ValueError('Failure-frame composition did not preserve exact decoded Native radiance')
        report['rawFallback'] = dict(pixels=int(np.prod(raw['SceneColor'].shape[:2])), exact=True)

        folder = output/'history'
        folder.mkdir()
        log = folder/'runtime.log'
        command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
            '-PathTracingHistoryValidation', str(folder), '-LogToFile', str(log),
            '-NrdPreview', '-NrdDiagnosticFailure', 'after-four-frames']
        lines = check_log(log, subprocess.run(command, cwd=ROOT, timeout=600))
        records = [json.loads(line[len('[PathTracingHistory] '):]) for line in lines if line.startswith('[PathTracingHistory] ')]
        completed = [line[len('[PathTracingHistoryCapture] '):] for line in lines if line.startswith('[PathTracingHistoryCapture] ')]
        validate_records(records, completed)
        if lines.count('[PathTracingHistoryComplete] captures='+str(len(CASES))) != 1:
            raise ValueError('Failure lifecycle incomplete')
        images = {}
        for record in records:
            path = Path(record['path'])
            meta, image = read_buffer(path)
            if (meta.get('resource') != 'PathTracing.Accumulation' or meta.get('format') != 2 or
                    (meta['width'], meta['height']) != (record['renderWidth'], record['renderHeight']) or
                    meta.get('sampleStartIndex') != record['frameSampleIndex'] or meta.get('randomSeed') != 11):
                raise ValueError('Failure lifecycle resource or state mismatch')
            assess_failure(meta, 'after-four-frames')
            attempt = meta.get('nrdLastEvaluation', {})
            if (attempt.get('attempted') is not True or attempt.get('recorded') is not False or
                    attempt.get('frameIndex') != 4 or attempt.get('dispatchCount') != 0):
                raise ValueError('Disabled backend was retried or reported a stale success')
            images[record['case']] = normalize_accumulation(image, record['accumulatedSamples'])
            record['sha256'] = sha(path)
        report['history'] = dict(command=command, records=records, assessment=assess_images(images),
            logSha256=sha(log), d3d12Errors=0)
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = ('Healthy-device initialization and late CPU-preparation failures only. '
        'No driver/device-loss injection and no recovery after command recording starts; no denoising-quality acceptance.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
