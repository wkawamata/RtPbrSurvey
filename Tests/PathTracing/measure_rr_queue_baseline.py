"""RRQ-01: isolated static queue baseline; no rendering-policy changes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess

from measure_rr_release import require_isolation

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values):
    values = sorted(values)
    if any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError('Invalid nonfinite/negative timing')
    if not values:
        return None
    return dict(count=len(values), mean=statistics.mean(values),
                p50=statistics.median(values), p95=values[int((len(values)-1)*.95)])


def parse(text, warmup, frames):
    rows, current = [], None
    cpu = 0.0
    for line in text.splitlines():
        match = re.match(r'\[FPS\] Frame \d+: .*\((\S+) ms\)', line)
        if match:
            cpu = float(match[1])
        match = re.match(r'\[PT Progress\] frame=(\d+) wall=(\S+)', line)
        if match:
            if current:
                rows.append(current)
            current = dict(frame=int(match[1]), wall=float(match[2]), cpu=cpu, passes={})
        if current is None:
            continue
        if line.startswith('[RenderGraphFrame] '):
            current.update(json.loads(line[len('[RenderGraphFrame] '):]))
        if line.startswith('[RR Packet] '):
            current['latency'] = {k: None if v == "N/A" else float(v) for k, v in
                                  (item.split('=') for item in line[len('[RR Packet] '):].split())}
        match = re.match(r'\[GPU\] Frame \d+: latest completed total (\S+) ms', line)
        if match:
            current['gpu'] = float(match[1])
        match = re.match(r'\[GPU Pass\] (.*): (\S+) ms', line)
        if match:
            current['passes'][match[1]] = float(match[2])
    if current:
        rows.append(current)
    selected = [r for r in rows if warmup <= r['frame'] <= warmup+frames]
    if len(selected) != frames+1:
        raise ValueError('Incomplete measurement interval')
    if any(b['rrCompleted'] < a['rrCompleted'] for a, b in zip(selected, selected[1:])):
        raise ValueError('Completion counter regressed')
    seconds = selected[-1]['wall']-selected[0]['wall']
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('Invalid observation interval')
    count = selected[-1]['rrCompleted']-selected[0]['rrCompleted']
    packets = {r['gpuPacket']: r for r in selected if r.get('gpuPacket') and 'gpu' in r}
    latency = {r['latency']['frameId']: r['latency'] for r in selected
               if r.get('latency', {}).get('frameId', 0) > 0}
    passes = {}
    for r in packets.values():
        for name, ms in r['passes'].items():
            passes.setdefault(name, []).append(ms)
    return dict(intervals=frames, seconds=seconds, completed=count,
                freshRrFps=count/seconds, displayFps=frames/seconds,
                updatesPerDisplay=count/frames, cpuMs=stats([r['cpu'] for r in selected]),
                retainedAgeMs=stats([r['rrRetainedAgeMs'] for r in selected if r.get('rrPublished', 0) > 0 and 'rrRetainedAgeMs' in r]),
                completedGpuPacketMs=stats([r['gpu'] for r in packets.values()]),
                gpuPassMs={n: stats(v) for n, v in passes.items()},
                packetLatencyMs={n: stats([r[n] for r in latency.values() if r.get(n) is not None]) for n in
                                 ['inputWaitMs', 'admittedWaitMs', 'evaluateCpuMs', 'outputWaitMs', 'totalMs', 'finalGpuWaitMs']},
                phaseCounts={n: sum(r['rrPhase'] == n for r in selected)
                             for n in sorted({r['rrPhase'] for r in selected})})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--comparison', choices=['baseline', 'safe'], default='baseline')
    parser.add_argument('--repeats', type=int, default=1)
    args = parser.parse_args()
    if args.warmup < 1 or args.frames < 10 or args.repeats < 1:
        parser.error('Need warmup and at least ten intervals')
    exe, out = args.exe.resolve(strict=True), args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT/'Assets/Scenes/WarmGlassEdges'
    assets = sorted(set(source.glob('*')) | set((ROOT/'Assets/Scenes/WarmGlassTable').glob('*')))
    report = dict(status='running', sourceCommit=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)),
        assetSha256={p.relative_to(ROOT).as_posix(): sha(p) for p in assets if p.is_file()},
        runtimeSha256={p.name: sha(p) for p in sorted(exe.parent.glob('*'))
                      if p.is_file() and (p.suffix == '.cso' or p.name.startswith(('sl.', 'nvngx_')))},
        settings=dict(warmup=args.warmup, intervals=args.frames, vsync=False, comparison=args.comparison, repeats=args.repeats,
                      samplesPerFrame=1, maxBounces=16, seed=7), runs=[])
    def save():
        (out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    save()
    try:
        for width, height in [(640, 360), (1280, 720)]:
            modes = [('strict', True), ('eager', False)] if args.comparison == 'baseline' else [('safe-serial', False), ('chained', False)]
            sequence = []
            for repeat in range(args.repeats):
                a, b = modes if repeat % 2 == 0 else modes[::-1]
                sequence.extend([a, b, b, a])
            for index, (schedule, strict) in enumerate(sequence):
                require_isolation()
                name=f'{width}x{height}-{index}-'+schedule
                log, png = out/(name+'.log'), out/(name+'.png')
                command=[str(exe), '-SceneFile', str(source/'scene.json'), '-WindowSize', str(width), str(height),
                         '-DisableVSync', '-PathTracingReconstruction', 'rr',
                         '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrThinExperiment',
                         '-PathTracingRrSmoothVolumeExperiment', '-PathTracingRrTransmissionMotionExperiment',
                         '-PathTracingRrContinuous', '-CapturePath', str(png), '-ExitAfterCapture',
                         '-CaptureAfterFrames', str(args.warmup+args.frames), '-LogToFile', str(log), '-LogFPS', '1']
                if strict:
                    command.append('-PathTracingRrStrictSchedule')
                elif schedule in ('safe-serial', 'chained'):
                    command += ['-PathTracingRrSchedule', schedule]
                startup=subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow=subprocess.SW_HIDE
                child=subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
                try:
                    code=child.wait(timeout=300)
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
                require_isolation()
                text=log.read_text(encoding='utf-8-sig')
                if code or '[ERROR]' in text or '[CORRUPTION]' in text:
                    raise RuntimeError(f'{name}: process/debug failure {code}')
                diag=json.loads(next(l[len('[PathTracing] '):] for l in text.splitlines()
                                     if l.startswith('[PathTracing] {')))
                if (diag['renderWidth'], diag['renderHeight']) != (width, height) or diag['d3d12DebugLayerEnabledByApp']:
                    raise RuntimeError('Unexpected resolution or enabled debug layer')
                rr=diag['ptRr']
                if diag['reconstructionOutput'] != 'rr' or not rr['outputVerified'] or rr['cancelledCount'] or rr['inputRejectedCount'] or rr.get('outputRejected', 0) or rr.get('sdkFailures', 0):
                    raise RuntimeError(f'{name}: RR output contract failure')
                if args.comparison == 'safe' and rr['chained'] != (schedule == 'chained'):
                    raise RuntimeError('Unexpected scheduler')
                if rr['resetCount'] != 1:
                    raise RuntimeError('Unexpected static history reset')
                if (diag['samplesPerFrame'],diag['maxBounces'],diag['randomSeed']) != (1,16,7):
                    raise RuntimeError('Fixture settings changed')
                row=dict(name=name, strict=strict, schedule=schedule, width=width, height=height, command=command,
                         diagnostics=diag, metrics=parse(text, args.warmup, args.frames), pngSha256=sha(png))
                report['runs'].append(row)
                save()
                print(name, 'freshRR', round(row['metrics']['freshRrFps'],3), flush=True)
        if sha(exe) != report['executableSha256']:
            raise RuntimeError('Executable changed during measurement')
        for relative, expected in report['assetSha256'].items():
            if sha(ROOT/relative) != expected:
                raise RuntimeError('Asset changed during measurement: '+relative)
        for name, expected in report['runtimeSha256'].items():
            if sha(exe.parent/name) != expected:
                raise RuntimeError('Runtime changed during measurement: '+name)
        report['status']='passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
