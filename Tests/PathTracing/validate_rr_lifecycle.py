"""Exercise continuous static RR, pause/resume, NRD switching and resize in one GPU process."""
import argparse
import json
from pathlib import Path
import subprocess

from validate_part1 import sha, write_json
from validate_rr_native import fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    output, source, exe = args.output.resolve(), args.source.resolve(strict=True), args.exe.resolve(strict=True)
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    fixture(source, output)
    log = output/'runtime.log'
    command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
               '-WindowSize', '256', '144', '-EnableDlssRayReconstruction',
               '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrLifecycleValidation', str(output),
               '-LogToFile', str(log)]
    report = dict(status='running', executableSha256=sha(exe), command=command, checkpoints=[], failures=[])
    write_json(output/'report.json', report)
    try:
        child = subprocess.Popen(command, cwd=Path(__file__).resolve().parents[2])
        try:
            code = child.wait(timeout=180)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
        report['exitCode'] = code
        text = log.read_text(encoding='utf-8-sig')
        errors = [line for line in text.splitlines()
                  if line.startswith(('[ERROR]', '[CORRUPTION]', '[PathTracingRrLifecycleFailure]'))]
        if code != 0 or errors:
            raise ValueError(f'Process={code}, errors={errors}')
        prefix = '[PathTracingRrLifecycle] '
        records = [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]
        expected = ['continuous', 'paused', 'resumed', 'native', 'nrd', 'rr-restored', 'pending-reset',
                    'camera-recovered', 'resized', 'resize-restored']
        if [item['case'] for item in records] != expected or '[PathTracingRrLifecycleComplete] stages=10' not in text:
            raise ValueError('Incomplete lifecycle checkpoints')
        for item in records:
            if item['case'] not in ('native', 'nrd') and (item['output'] != 'rr' or not item['verified']):
                raise ValueError('RR checkpoint did not publish verified output')
        if records[0]['continuous'] < 2 or records[0]['resets'] != 1:
            raise ValueError('Continuous history was not exercised')
        if records[1]['nativeCalls'] != records[0]['nativeCalls'] or records[1]['publishedFrameId'] != records[0]['publishedFrameId']:
            raise ValueError('Paused production was re-evaluated')
        if records[3]['output'] != 'native' or records[4]['output'] != 'nrd':
            raise ValueError('Backend switch was not reflected in presentation')
        if records[6]['cancelled'] <= records[5]['cancelled']:
            raise ValueError('In-flight cancellation was not exercised')
        if (records[8]['width'], records[8]['height']) != (320, 180) or (records[9]['width'], records[9]['height']) != (256, 144):
            raise ValueError('Resize output dimensions did not update')
        image = output/'viewport.png'
        if not image.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Final verified viewport capture missing')
        report.update(status='passed', checkpoints=records, d3d12Errors=0, viewportSha256=sha(image))
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    write_json(output/'report.json', report)
    print(json.dumps(report), flush=True)
    if report['status'] != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
