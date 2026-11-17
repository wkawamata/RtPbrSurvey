"""RRQ-03 Debug smoke: scheduling ownership and verified publication, not performance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from measure_rr_release import require_isolation
from measure_rr_queue_baseline import parse

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    exe, out = args.exe.resolve(strict=True), args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    require_isolation()
    out.mkdir(parents=True, exist_ok=True)
    executable_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    report = dict(status='running', executableSha256=executable_hash, runs=[])
    try:
        for name, schedule, scene, motion in [
            ('eager', None, 'WarmGlassEdges', False),
            ('safe-serial', 'safe-serial', 'WarmGlassEdges', False),
            ('chained', 'chained', 'WarmGlassEdges', False),
            ('chained-camera', 'chained', 'WarmGlassTable', True),
        ]:
            require_isolation()
            log, capture = out / (name + '.log'), out / (name + '.png')
            command = [str(exe), '-SceneFile', str(ROOT / 'Assets/Scenes' / scene / 'scene.json'),
                       '-WindowSize', '640', '360', '-PathTracingReconstruction', 'rr',
                       '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrContinuous',
                       '-PathTracingRrThinExperiment', '-PathTracingRrSmoothVolumeExperiment',
                       '-PathTracingRrRoughVolumeExperiment', '-PathTracingRrTransmissionMotionExperiment',
                       '-CapturePath', str(capture), '-CaptureAfterFrames', '120', '-ExitAfterCapture',
                       '-LogToFile', str(log), '-LogFPS', '1']
            if schedule:
                command += ['-PathTracingRrSchedule', schedule]
            if motion:
                command += ['-ReflectionOrbitDegrees', '8', '-ReflectionOrbitFrames', '90']
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
            try:
                code = child.wait(timeout=180)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            require_isolation()
            text = log.read_text(encoding='utf-8-sig')
            if code or '[ERROR]' in text or '[CORRUPTION]' in text:
                raise RuntimeError(f'{name}: process/debug failure {code}')
            diag = json.loads(next(line[len('[PathTracing] '):] for line in text.splitlines()
                                   if line.startswith('[PathTracing] {')))
            rr = diag['ptRr']
            assert diag['reconstructionOutput'] == 'rr' and rr['outputVerified']
            assert rr['completedCount'] > 5 and rr['cancelledCount'] == 0 and rr['inputRejectedCount'] == 0
            assert rr['chained'] == (schedule == 'chained')
            graphs = [json.loads(line[len('[RenderGraphFrame] '):]) for line in text.splitlines()
                      if line.startswith('[RenderGraphFrame] ')]
            combined = [g for g in graphs if 'PathTracingPass' in g['scheduled'] and
                        'PT DLSS RR Native' in g['scheduled']]
            if schedule == 'chained':
                assert combined and not any(g['rrPhase'] == 'input-gpu-pending' for g in graphs)
                for graph in combined:
                    names = graph['scheduled']
                    assert all(names.count(n) == 1 for n in ['PathTracingPass',
                        'PT RR Input Conversion Diagnostics', 'PT RR Input Admission', 'PT DLSS RR Native'])
                    assert names.index('PathTracingPass') < names.index('PT RR Input Conversion Diagnostics')
                    assert names.index('PT RR Input Conversion Diagnostics') < names.index('PT RR Input Admission')
                    assert names.index('PT RR Input Admission') < names.index('PT DLSS RR Native')
                assert 'inputWaitMs=N/A' in text and 'finalGpuWaitMs=' in text
            else:
                assert not combined
            assert capture.stat().st_size > 0
            metrics = parse(text, 30, 90)
            report['runs'].append(dict(name=name, command=command, diagnostics=diag,
                                       combinedFrames=len(combined), metrics=metrics))
            (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
            print(name, 'verified outputs:', rr['completedCount'], 'combined frames:', len(combined), flush=True)
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == executable_hash
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
