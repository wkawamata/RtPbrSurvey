"""Bounded integration rerun on an explicit executable; no new quality thresholds."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from validate_history import CASES, validate_records
from validate_inputs import read_buffer
from validate_nrd_camera_motion import assess_frame, assess_motion
from validate_nrd_failure_fallback import assess_failure
from validate_nrd_presentation import RELAX, INCOMPATIBLE, assess_pair
from validate_nrd_runtime_history import assess_history
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]


def run(command, log, timeout=600):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
    try:
        code = child.wait(timeout=timeout)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    lines = log.read_text(encoding='utf-8-sig').splitlines()
    if code or any(token in line for line in lines for token in
            ('[ERROR]', '[CORRUPTION]', '[PathTracingHistoryFailure]')):
        raise ValueError('Runtime or D3D12 error: '+str(log))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'source', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    exe, source = args.exe.resolve(strict=True), args.source.resolve(strict=True)
    output = args.output.resolve()
    if source == output or source in output.parents or exe.parent in output.parents or (
            output.exists() and any(output.iterdir())):
        parser.error('Output must be separate and new')
    output.mkdir(parents=True, exist_ok=True)
    preset_path = source/('preset.json' if (source/'preset.json').exists() else 'on.json')
    scene = json.loads((source/'scene.json').read_text(encoding='utf-8-sig'))
    preset = json.loads(preset_path.read_text(encoding='utf-8-sig'))
    preset['pathTracing'].update(accumulate=True, samplesPerFrame=1, randomSeed=11,
        pixelCenterPrimary=True, denoiserDistanceDiagnostics=True, debugOutput=3, nrdPreview=True)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    files = [exe, Path(__file__), source/'scene.json', preset_path, output/'scene.json', output/'preset.json']
    files += sorted(exe.parent.glob('*.cso'))
    files += [Path(__file__).with_name(name) for name in ('validate_history.py', 'validate_inputs.py',
        'validate_nrd_camera_motion.py', 'validate_nrd_failure_fallback.py',
        'validate_nrd_presentation.py', 'validate_nrd_runtime_history.py')]
    artifacts = {str(path): sha(path) for path in files}
    report = dict(status='running', scope='integration-only', qualityVerdict='not-evaluated',
        fullIntegrationReady=False, artifacts=artifacts, records=[], failures=[])
    write_json(output/'report.json', report)

    def capture(label, resource, samples=12, failure='', fixture=output, orbit=0, continuous=False):
        path, log = output/(label+('.ptbuf' if resource else '.png')), output/(label+'.log')
        command = [str(exe), '-SceneFile', str(fixture/'scene.json'), '-RenderPreset', str(fixture/'preset.json'),
            '-EnablePathTracing',
            '-CapturePath', str(path), '-LogToFile', str(log), '-ExitAfterCapture']
        if resource:
            command += ['-DebugPreviewResource', 'PathTracing.'+resource]
        if orbit or continuous:
            command += ['-CaptureAfterFrames', '30']
        else:
            command += ['-PathTracingSamples', str(samples)]
        if failure:
            command += ['-NrdDiagnosticFailure', failure]
        if orbit:
            command += ['-ReflectionOrbitDegrees', str(orbit), '-ReflectionOrbitFrames', '20']
        if continuous:
            command += ['-NrdContinuousCameraHistory']
        lines = run(command, log)
        runtime = [json.loads(line[len('[PathTracing] '):]) for line in lines if line.startswith('[PathTracing] ')]
        if len(runtime) != 1 or sum(line.startswith('[Capture] ') and 'complete=true' in line for line in lines) != 1:
            raise ValueError('Missing unique capture completion: '+label)
        state = runtime[0]
        if state.get('d3d12DebugLayerEnabledByApp') is not True or state.get('d3d12DebugLayerRequested') is not True:
            raise ValueError('D3D12 validation is not enabled: '+label)
        if not orbit and not continuous and (state.get('targetSamples') != samples or
                state.get('accumulatedSamples') != samples or state.get('samplesPerFrame') != 1 or
                state.get('randomSeed') != 11):
            raise ValueError('Wrong capture sample state: '+label)
        meta, image = read_buffer(path) if resource else ({}, np.array(Image.open(path).convert('RGB')))
        if resource and (meta.get('resource') != 'PathTracing.'+resource or meta.get('format') !=
                (34 if resource == 'MotionVectors' else 10 if resource == 'SceneColor' else 2)):
            raise ValueError('Wrong resource/format: '+label)
        record = dict(label=label, command=command, sha256=sha(path), logSha256=sha(log),
            metadata=meta, diagnostics=state, d3d12Errors=0)
        report['records'].append(record)
        write_json(output/'report.json', report)
        return meta, image, record

    try:
        folder = output/'history'
        folder.mkdir()
        log = folder/'runtime.log'
        command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
            '-PathTracingHistoryValidation', str(folder), '-LogToFile', str(log),
            '-DebugPreviewResource', 'PathTracing.NrdSceneColor', '-NrdPreview']
        lines = run(command, log)
        records = [json.loads(line[len('[PathTracingHistory] '):]) for line in lines if line.startswith('[PathTracingHistory] ')]
        completed = [line[len('[PathTracingHistoryCapture] '):] for line in lines if line.startswith('[PathTracingHistoryCapture] ')]
        validate_records(records, completed)
        if lines.count('[PathTracingHistoryComplete] captures='+str(len(CASES))) != 1:
            raise ValueError('Missing lifecycle completion')
        images = {}
        for record in records:
            path = Path(record['path']).resolve(strict=True)
            if path.parent != folder:
                raise ValueError('Lifecycle capture escaped its output folder')
            meta, image = read_buffer(path)
            expected = 'Native fallback: awaiting current NRD composition' if record['case'] == 'reset-paused-0' else (
                INCOMPATIBLE if record['samplesPerFrame'] != 1 else RELAX)
            if (meta.get('resource') != 'PathTracing.NrdSceneColor' or meta.get('format') != 2 or
                    meta.get('sampleStartIndex') != record['frameSampleIndex'] or meta.get('randomSeed') != 11 or
                    meta.get('samplesPerFrame') != record['samplesPerFrame'] or
                    (meta.get('width'), meta.get('height')) != (record['renderWidth'], record['renderHeight']) or
                    meta.get('primarySampling') != 'pixel-center' or meta.get('nrdPreviewRequested') is not True or
                    meta.get('nrdPresentation') != expected or meta.get('nrdFailure') != ''):
                raise ValueError('Invalid lifecycle metadata: '+record['case'])
            images[record['case']] = image.astype(np.float32)
            record.update(sha256=sha(path), metadata=meta)
        report['history'] = dict(command=command, logSha256=sha(log), records=records,
            assessment=assess_history(images, composed=True), d3d12Errors=0)
        del images
        write_json(output/'report.json', report)
        print('passed 19-stage lifecycle', flush=True)
        _, native, _ = capture('native-control', 'SceneColor')
        native_fixture = output/'native-fixture'
        write_json(native_fixture/'scene.json', scene)
        native_preset = json.loads(json.dumps(preset))
        native_preset['pathTracing']['nrdPreview'] = False
        write_json(native_fixture/'preset.json', native_preset)
        for name in ('scene.json', 'preset.json'):
            artifacts[str(native_fixture/name)] = sha(native_fixture/name)
        _, off_native, record = capture('native-preview-off', 'SceneColor', fixture=native_fixture)
        if record['diagnostics'].get('nrdPresentation') != 'Native PT' or not np.array_equal(native, off_native):
            raise ValueError('NRD enablement changed the Native reference buffer')
        _, native_final, _ = capture('native-final', '', fixture=native_fixture)
        for mode in ('initialization', 'last-dispatch', 'after-four-frames'):
            _, fallback_final, record = capture('fallback-'+mode+'-final', '', failure=mode)
            record['assessment'] = dict(failure=assess_failure(record['diagnostics'], mode),
                finalViewport=assess_pair(native_final, fallback_final, qualified=False))
        for mode, sample in (('last-dispatch', 1), ('after-four-frames', 5)):
            _, expected_native, _ = capture('failure-frame-native-'+mode, 'SceneColor', samples=sample, fixture=native_fixture)
            _, actual, record = capture('fallback-'+mode, 'NrdSceneColor', samples=sample, failure=mode)
            record['assessment'] = assess_failure(record['diagnostics'], mode)
            if not np.array_equal(expected_native, actual):
                raise ValueError('Failure fallback is not exact Native: '+mode)
        print('passed exact Native fallback in three failure modes', flush=True)
        motion = output/'motion-fixture'
        write_json(motion/'scene.json', json.loads((ROOT/'Assets/Scene/PathTracingValidation/14-nrd-disocclusion/scene.json').read_text(encoding='utf-8-sig')))
        write_json(motion/'preset.json', preset)
        for name in ('scene.json', 'preset.json'):
            artifacts[str(motion/name)] = sha(motion/name)
        motion_scene = json.loads((motion/'scene.json').read_text(encoding='utf-8-sig'))
        for label, angle, continuous in (('static', 0, True), ('positive', 20, True),
                ('negative', -20, True), ('reset-control', 20, False)):
            meta, image, record = capture('motion-'+label, 'MotionVectors', fixture=motion, orbit=angle, continuous=continuous)
            if meta.get('nrdPresentation') != RELAX or meta.get('nrdFailure') != '':
                raise ValueError('Motion capture did not execute NRD')
            record['assessment'] = dict(history=assess_frame(meta, continuous, bool(angle)),
                motion=assess_motion(meta, image, motion_scene, bool(angle)))
        if any(sha(Path(path)) != digest for path, digest in artifacts.items()):
            raise ValueError('Delivery artifacts changed during execution')
        report['status'] = 'passed-integration-rerun'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = ('Fresh integration checks on one GPU. No new high-sample reference, universal scene-quality '
        'claim, final human approval, device-loss injection or moving-object validation.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-integration-rerun'


if __name__ == '__main__':
    raise SystemExit(main())
