"""Validate the opt-in retained PT frame -> native RR -> normal viewport path."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
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
    report = dict(status='running', executableSha256=sha(exe), records=[], failures=[])
    write_json(output/'report.json', report)
    try:
        cases = [('rr-output', True, 'PathTracing.RrOutput', True),
                 ('rr-viewport', True, '', True),
                 ('native-off', False, '', False),
                 ('background', True, '', False),
                 ('non-mirror', True, '', False)]
        for name, opt_in, resource, expected_rr in cases:
            folder = output/name
            fixture(output, folder)
            if name == 'background':
                gltf = json.loads((folder/'box.gltf').read_text())
                gltf['scenes'][0]['nodes'].remove(1)
                write_json(folder/'box.gltf', gltf)
            elif name == 'non-mirror':
                gltf = json.loads((folder/'box.gltf').read_text())
                gltf['materials'][0]['pbrMetallicRoughness'].update(metallicFactor=.5, roughnessFactor=.3)
                write_json(folder/'box.gltf', gltf)
            capture = folder/('output.ptbuf' if resource else 'viewport.png')
            log = folder/'runtime.log'
            command = [str(exe), '-SceneFile', str(folder/'scene.json'), '-RenderPreset', str(folder/'preset.json'),
                       '-WindowSize', '256', '144', '-PathTracingReconstruction', 'rr',
                       '-CapturePath', str(capture), '-CaptureAfterFrames', '60', '-ExitAfterCapture',
                       '-LogToFile', str(log)]
            if opt_in:
                command += ['-EnableDlssRayReconstruction', '-EnableExperimentalNativeRayReconstruction']
            if resource:
                command += ['-DebugPreviewResource', resource]
            print('Running '+name, flush=True)
            child = subprocess.Popen(command, cwd=Path(__file__).resolve().parents[2])
            try:
                code = child.wait(timeout=180)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            if not log.exists():
                raise ValueError(f'{name}: process={code}, exited before PT log creation')
            text = log.read_text(encoding='utf-8-sig')
            errors = [line for line in text.splitlines() if line.startswith(('[ERROR]', '[CORRUPTION]'))]
            if code != 0 or errors:
                raise ValueError(f'{name}: process={code}, D3D12 errors={errors}')
            records = [json.loads(line[len('[PathTracing] '):]) for line in text.splitlines()
                       if line.startswith('[PathTracing] {')]
            if not records:
                raise ValueError('No PT diagnostics: '+name)
            diagnostics = records[-1]
            rr = diagnostics['ptRr']
            if expected_rr:
                if (diagnostics['reconstructionOutput'] != 'rr' or diagnostics['reconstructionStatus'] != 'ready' or
                    not rr['outputVerified'] or rr['phase'] != 'retained-output-ready' or
                    rr['nativeEvaluateCallCount'] != 1 or rr['failure']):
                    raise ValueError('RR did not reach verified presentation: '+json.dumps(diagnostics))
            else:
                if diagnostics['reconstructionOutput'] != 'native' or rr['nativeEvaluateCallCount'] != 0:
                    raise ValueError('Rejected/disabled input reached SDK or RR presentation')
                if opt_in and (rr['phase'] != 'native-fallback' or rr['failure'] != 'unsupported RR input pixels'):
                    raise ValueError('Unsupported profile was not rejected by whole-frame admission')
            if resource:
                meta, values = read_buffer(capture)
                if meta['resource'] != resource or meta['format'] != 2 or not np.isfinite(values).all():
                    raise ValueError('Wrong/nonfinite native output texture')
            elif not capture.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
                raise ValueError('Viewport capture missing')
            report['records'].append(dict(case=name, command=command, exitCode=code, d3d12Errors=0,
                                           diagnostics=diagnostics, captureSha256=sha(capture)))
            write_json(output/'report.json', report)
            print('Passed '+name, flush=True)
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['failures'].append(str(error))
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failures=report['failures'])), flush=True)
    if report['status'] != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
