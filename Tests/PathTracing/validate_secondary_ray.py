"""Validate diagnostic secondary-ray data; not a denoiser hit-distance contract."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]


def assess(data, albedo, mode, ray_max=100.0):
    if mode not in ('one-segment', 'metal', 'indirect'):
        raise ValueError('Unknown secondary validation mode')
    if not np.isfinite(ray_max) or ray_max <= 0:
        raise ValueError('Invalid secondary ray maximum distance')
    meta, values = data
    other_meta, color = albedo
    if meta['resource'] != 'PathTracing.SecondaryRayData' or meta['format'] != 2:
        raise ValueError('Unexpected secondary resource/format')
    if other_meta['resource'] != 'PathTracing.Albedo' or other_meta['format'] != 10:
        raise ValueError('Unexpected validity resource/format')
    for key in ('width', 'height', 'randomSeed', 'sampleStartIndex',
                'viewProjection', 'previousViewProjection'):
        if meta[key] != other_meta[key]:
            raise ValueError('Secondary cohort mismatch: ' + key)
    shape = (meta['height'], meta['width'], 4)
    if (values.shape != shape or color.shape != shape or not np.isfinite(values).all()
            or not np.isfinite(color).all() or not np.isin(color[..., 3], [0, 1]).all()):
        raise ValueError('Invalid secondary values')
    distance, proposal, status, reserved = np.moveaxis(values, -1, 0)
    untraced, hit, miss = status == 0, status == 1, status == 2
    primary_miss = color[..., 3] == 0
    checks = dict(status=bool((untraced | hit | miss).all()),
                  reserved=bool((reserved == 0).all()),
                  untraced=bool((values[untraced] == 0).all()),
                  proposals=bool(((proposal[~untraced] == 1) | (proposal[~untraced] == 2)).all()),
                  hitDistance=bool(((distance[hit] > 0) & (distance[hit] <= ray_max)).all()),
                  missDistance=bool((distance[miss] == ray_max).all()),
                  primaryMiss=bool((values[primary_miss] == 0).all()))
    if mode == 'one-segment':
        checks['noContinuation'] = bool(untraced.all())
    else:
        checks['hasContinuation'] = bool((~untraced).any())
        checks['hasMiss'] = bool(miss.any())
        if mode == 'metal':
            checks['metalUsesSpecularProposal'] = bool((proposal[~untraced] == 2).all())
        if mode == 'indirect':
            checks['hasSecondaryHit'] = bool(hit.any())
            checks['bothProposals'] = bool((proposal == 1).any() and (proposal == 2).any())
    return dict(passed=all(checks.values()), checks=checks,
                hitPixels=int(hit.sum()), missPixels=int(miss.sum()), untracedPixels=int(untraced.sum()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    args = parser.parse_args()
    output, exe = args.output.resolve(), args.exe.resolve(strict=True)
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    report = dict(schemaVersion=1, status='running', exeSha256=sha(exe),
                  shaderSha256=sha(ROOT/'Shaders/shaders_PathTracing.hlsl'),
                  scriptSha256=sha(Path(__file__)), records=[], failures=[],
                  limitation='First sampled secondary segment, proposal classification only; not NRD packed distance.')
    for mode, fixture, segments in [('one-segment', 'roughness', 1),
                                    ('metal', 'roughness', 2), ('indirect', 'two-surface-indirect', 3)]:
        directory = output/mode
        source = ROOT/'Assets/Scenes/PathTracingValidation'/fixture
        scene = json.loads((source/'scene.json').read_text())
        scene['renderPreset'] = 'preset.json'
        preset = json.loads((source/'render-preset.json').read_text())
        preset['pathTracing'].update(accumulate=False, samplesPerFrame=1, maxBounces=segments,
                                    randomSeed=7, debugOutput=3)
        preset['shadow']['rayTMax'] = 100.0
        write_json(directory/'scene.json', scene)
        write_json(directory/'preset.json', preset)
        record = dict(mode=mode, captures=[])
        report['records'].append(record)
        buffers = {}
        try:
            for name in ('SecondaryRayData', 'Albedo'):
                path, log = directory/(name+'.ptbuf'), directory/(name+'.log')
                command = [str(exe), '-SceneFile', str(directory/'scene.json'),
                           '-RenderPreset', str(directory/'preset.json'), '-EnablePathTracing',
                           '-DebugPreviewResource', 'PathTracing.'+name, '-CapturePath', str(path),
                           '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
                child = subprocess.Popen(command, cwd=ROOT)
                try:
                    if child.wait(timeout=180) != 0:
                        raise RuntimeError('Application failed')
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
                text = log.read_text(encoding='utf-8-sig')
                if '[ERROR]' in text or '[CORRUPTION]' in text:
                    raise RuntimeError('D3D12 capture errors')
                buffers[name] = read_buffer(path)
                record['captures'].append(dict(resource=name, command=command, sha256=sha(path), d3d12Errors=0))
            record['assessment'] = assess(buffers['SecondaryRayData'], buffers['Albedo'], mode)
            if not record['assessment']['passed']:
                raise ValueError('Secondary checks failed')
            print(mode + ': passed', flush=True)
        except Exception as error:
            report['failures'].append(dict(mode=mode, error=str(error)))
        write_json(output/'report.json', report)
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report['failures']), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
