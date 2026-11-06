"""Measure Release Native/RR GPU observations and fresh RR completion rate."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def summarize(values):
    values = sorted(values)
    if not values:
        return None
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError('Invalid GPU timing')
    return dict(count=len(values), meanMs=statistics.mean(values), medianMs=statistics.median(values),
                p95Ms=values[int((len(values)-1)*.95)])


def parse_log(text, warmup, frames):
    observations, progress, diagnostics = [], {}, []
    current = None
    packets = {}
    for line in text.splitlines():
        if line.startswith('[PathTracing] {'):
            diagnostics.append(json.loads(line[len('[PathTracing] '):]))
            break  # exclude capture/readback work
        match = re.fullmatch(r'\[PT Progress\] frame=(\d+) wall=(\S+) sampleIndex=(\d+) rrCompleted=(\d+) rrResets=(\d+)', line)
        if match:
            frame, wall, sample, completed, resets = match.groups()
            progress[int(frame)] = dict(wall=float(wall), sample=int(sample), completed=int(completed), resets=int(resets))
        if line.startswith('[RR Packet] '):
            packet = {key:float(value) for key,value in
                      (item.split('=') for item in line[len('[RR Packet] '):].split())}
            if any(not math.isfinite(value) or value < 0 for value in packet.values()):
                raise ValueError('Invalid RR packet latency')
            if packet['completed'] > 0:
                packets[int(packet['completed'])] = packet
        match = re.fullmatch(r'\[GPU\] Frame (\d+): latest completed total (\S+) ms', line)
        if match:
            current = dict(frame=int(match[1]), total=float(match[2]), passes={})
            observations.append(current)
        match = re.fullmatch(r'\[GPU Pass\] (.+): (\S+) ms', line)
        if match and current is not None:
            current['passes'][match[1]] = float(match[2])
    selected = observations[warmup:warmup+frames]
    if len(selected) != frames or not diagnostics:
        raise ValueError('Missing measurement observations or capture diagnostics')
    first, last = (progress[selected[i]['frame']] for i in (0, -1))
    elapsed = last['wall']-first['wall']
    if elapsed <= 0 or last['resets'] != first['resets']:
        raise ValueError('Invalid elapsed time or history reset within measurement')
    completed_packets = [packet for completed,packet in packets.items()
                         if first['completed'] < completed <= last['completed']]
    packet_fields = ('inputWaitMs','admittedWaitMs','evaluateCpuMs','outputWaitMs','totalMs',
                     'inputPolls','outputPolls')
    packet_latency = dict(count=len(completed_packets),
                          means={field:statistics.mean(p[field] for p in completed_packets)
                                 for field in packet_fields} if completed_packets else {})
    names = sorted({name for row in selected for name in row['passes']})
    return dict(packetLatency=packet_latency, gpuTotal=summarize([row['total'] for row in selected]),
                activePasses={name:summarize([row['passes'][name] for row in selected if name in row['passes']]) for name in names},
                measuredSeconds=elapsed, rrCompletedDelta=last['completed']-first['completed'],
                freshRrOutputsPerSecond=(last['completed']-first['completed'])/elapsed,
                displayObservationsPerSecond=(len(selected)-1)/elapsed,
                observations=selected, diagnostics=diagnostics[-1])


def require_isolation():
    command = ['powershell', '-NoProfile', '-Command',
               '@(Get-Process RtPbrSurvey -ErrorAction SilentlyContinue | Select-Object Id,Path) | ConvertTo-Json -Compress']
    result = subprocess.run(command, check=True, capture_output=True, text=True,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.stdout.strip() and json.loads(result.stdout):
        raise RuntimeError('Another RtPbrSurvey is running; close it before measuring. '+result.stdout.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'bin/x64/Release/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup', type=int, default=60)
    parser.add_argument('--frames', type=int, default=240)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.warmup < 1 or args.frames < 2 or args.repeats < 1:
        parser.error('Need positive warmup/repeats and at least two measurement frames')
    exe, output = args.exe.resolve(strict=True), args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be an empty directory')
    require_isolation()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'Assets/Scenes/PathTracingValidation/rr-quality'
    report = dict(status='running', commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(), runs=[], failures=[],
                  limits=['Latest completed GPU timestamps may repeat; these are display-loop observations.',
                          'Fresh RR outputs/sec uses completion-counter differences and monotonic wall time.',
                          'Static scene, pixel-center, 4 bounces, Accumulate OFF; not a quality comparison.',
                          'Native and RR have different production cadence; display FPS alone is insufficient.'])
    def save():
        (output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    cases = [(backend,width,height,spp) for backend in ('native','rr')
             for width,height in ((1280,720),(1920,1080)) for spp in (1,4)]
    save()
    try:
        for repeat in range(args.repeats):
            schedule = cases[:]
            random.Random(84+repeat).shuffle(schedule)
            for backend,width,height,spp in schedule:
                require_isolation()
                name = f'{backend}-{width}x{height}-s{spp}-r{repeat}'
                folder = output/name
                folder.mkdir()
                scene = json.loads((source/'scene.json').read_text(encoding='utf-8'))
                preset = json.loads((source/'render-preset.json').read_text(encoding='utf-8'))
                scene['renderPreset'] = 'preset.json'
                for asset in scene['assets']:
                    asset['path'] = os.path.relpath(source/asset['path'],folder).replace('\\','/')
                preset['pathTracing'].update(samplesPerFrame=spp,pixelCenterPrimary=True,maxBounces=4,accumulate=False)
                for filename,data in (('scene.json',scene),('preset.json',preset)):
                    (folder/filename).write_text(json.dumps(data,indent=2),encoding='utf-8')
                command = [str(exe),'-SceneFile',str(folder/'scene.json'),'-PathTracingReconstruction',backend,
                           '-WindowSize',str(width),str(height),'-DisableVSync','-DisableD3D12DebugLayer',
                           '-LogFPS','1','-LogToFile',str(folder/'runtime.log'),'-CapturePath',str(folder/'viewport.png'),
                           '-CaptureAfterFrames',str(args.warmup+args.frames+20),'-ExitAfterCapture']
                if backend == 'rr':
                    command += ['-EnableDlssRayReconstruction','-EnableExperimentalNativeRayReconstruction','-PathTracingRrContinuous']
                print('Running '+name,flush=True)
                child = subprocess.Popen(command,cwd=ROOT)
                try:
                    code = child.wait(timeout=240)
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
                require_isolation()
                text = (folder/'runtime.log').read_text(encoding='utf-8-sig')
                if code or any(line.startswith(('[ERROR]','[CORRUPTION]')) for line in text.splitlines()):
                    raise RuntimeError('Run failed: '+name)
                result = parse_log(text,args.warmup,args.frames)
                diagnostics = result['diagnostics']
                if (diagnostics['renderWidth'], diagnostics['renderHeight']) != (width, height):
                    raise ValueError('Unexpected render resolution: '+name)
                if diagnostics['d3d12DebugLayerEnabledByApp']:
                    raise ValueError('Debug layer remained enabled: '+name)
                if result['diagnostics']['reconstructionOutput'] != backend:
                    raise ValueError('Unexpected backend/fallback: '+name)
                if backend == 'rr' and result['rrCompletedDelta'] < 10:
                    raise ValueError('Insufficient fresh RR outputs: '+name)
                report['runs'].append(dict(name=name,backend=backend,width=width,height=height,spp=spp,
                                           repeat=repeat,command=command,**result))
                save()
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['failures'].append(str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
