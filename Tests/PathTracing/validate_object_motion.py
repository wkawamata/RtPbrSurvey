"""Validate native primary-hit object motion with a fixed camera and translated plane."""
import argparse
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone
import numpy as np
from validate_inputs import ROOT, read_buffer, plane_hits, project
from validate_part1 import write_json, sha


def expected_motion(meta, world, delta):
    current = np.asarray(meta['singleInstanceWorld'], dtype=float)
    previous = np.asarray(meta['singleInstancePreviousWorld'], dtype=float)
    for matrix in (current, previous):
        if matrix.shape != (4, 4) or not np.isfinite(matrix).all():
            raise ValueError('Invalid object matrix')
    matrix_delta = np.zeros((4, 4))
    matrix_delta[0, 3] = delta
    if not np.allclose(current-previous, matrix_delta, atol=1e-6, rtol=0):
        raise ValueError('Actual object delta does not match requested motion')
    if meta['viewProjection'] != meta['previousViewProjection']:
        raise ValueError('Camera moved during fixed-camera validation')
    previous_hits = world - np.array([delta, 0, 0])
    motion = (project(previous_hits, meta['previousViewProjection']) -
              project(world, meta['viewProjection']))[:, :2]
    if not np.isfinite(motion).all():
        raise ValueError('Nonfinite projected motion')
    return motion


def analyze(path, delta):
    meta, raw = read_buffer(path)
    if meta['resource'] != 'PathTracing.MotionVectors' or meta['format'] != 34:
        raise ValueError('Expected native RG16 MotionVectors')
    xs, ys = np.meshgrid(np.arange(16, meta['width']-16, 4), np.arange(16, meta['height']-16, 4))
    xs, ys = xs.ravel(), ys.ravel()
    world, _, _ = plane_hits(meta, xs, ys)
    expected = expected_motion(meta, world, delta)
    observed = raw[ys, xs]
    error = abs(observed-expected)
    bound = 2*np.abs(np.spacing(abs(expected).astype(np.float16)).astype(float)) + 3e-6
    if delta and (np.max(abs(expected[:, 0])) < 1e-5 or np.max(abs(observed[:, 0])) < 1e-5):
        raise ValueError('Requested motion produced no measurable vector')
    return meta, dict(pixelCount=len(xs), maxAbsoluteError=float(error.max()),
        absoluteThresholdPassed=bool((error <= 2e-5).all()),
        halfPrecisionBound='2 ULP(expected half) + 3e-6 NDC',
        maxBoundNormalizedError=float((error/bound).max()), passed=bool((error <= bound).all()),
        expectedMean=expected.mean(axis=0).tolist(), observedMean=observed.mean(axis=0).tolist())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--analyze-only', action='store_true')
    args = parser.parse_args()
    args.exe = args.exe.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    fixture = ROOT/'Assets/Scenes/PathTracingValidation/input-plane'
    report = dict(schemaVersion=1, generatedUtc=datetime.now(timezone.utc).isoformat(),
        executable=str(args.exe), executableSha256=sha(args.exe),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        analyzeOnly=args.analyze_only,
        dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        sourceSha256={p: sha(ROOT/p) for p in ['App/RtPbrSurveyApp.cpp', 'Engine/RtPbrSurveyEngine.cpp',
            'Platform/CommandLineOptions.cpp', 'Shaders/shaders_PathTracing.hlsl',
            'Tests/PathTracing/validate_object_motion.py']},
        sceneSha256=sha(fixture/'scene.json'), presetSha256=sha(fixture/'render-preset.json'),
        records=[], failures=[], status='running')
    for name, delta in [('static', 0), ('positive-x', .05), ('negative-x', -.05)]:
        path = args.output/(name+'.ptbuf')
        log = args.output/(name+'.log')
        command = [str(args.exe), '-SceneFile', str(fixture/'scene.json'), '-RenderPreset',
            str(fixture/'render-preset.json'), '-EnablePathTracing', '-PathTracingSeed', '7',
            '-PathTracingObjectMotionX', str(delta), '-DebugPreviewResource', 'PathTracing.MotionVectors',
            '-CapturePath', str(path), '-CaptureAfterFrames', '30', '-LogToFile', str(log), '-ExitAfterCapture']
        record = dict(name=name, deltaPerFrame=delta, command=command)
        report['records'].append(record)
        try:
            if not args.analyze_only:
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
                try:
                    code = child.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                    raise
                if code:
                    raise RuntimeError(f'App exit {code}')
            errors = [line for line in log.read_text(encoding='utf-8-sig').splitlines()
                      if '[ERROR]' in line or '[CORRUPTION]' in line]
            if errors:
                raise RuntimeError(str(errors[:2]))
            meta, result = analyze(path, delta)
            record.update(metadata=meta, result=result, sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
            if not result['passed']:
                raise RuntimeError('Object motion numeric comparison failed')
        except Exception as error:
            record['failure'] = str(error)
            report['failures'].append(dict(name=name, error=str(error)))
        print(name, json.dumps(record.get('result', record.get('failure'))), flush=True)
        write_json(args.output/'report.json', report)
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(args.output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
