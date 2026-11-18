"""RRQ-04 chained recovery: rejected output isolation, history reset and lifecycle."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from measure_rr_release import require_isolation
from validate_inputs import read_buffer

ROOT = Path(__file__).resolve().parents[2]
FAULTS = ['input-one', 'input-all', 'output-nonfinite', 'output-incomplete',
          'stale-input', 'stale-output', 'sdk-post-record']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-checkpoints', type=int, choices=range(1,32))
    args = parser.parse_args()
    exe, out = args.exe.resolve(strict=True), args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    require_isolation()
    out.mkdir(parents=True, exist_ok=True)
    log = out / 'd3d12.log'
    command = [str(exe), '-SceneFile', str(ROOT / 'Assets/Scenes/WarmGlassEdges/scene.json'),
               '-WindowSize', '256', '144', '-EnableExperimentalNativeRayReconstruction',
               '-PathTracingRrThinExperiment', '-PathTracingRrSmoothVolumeExperiment',
               '-PathTracingRrTransmissionMotionExperiment', '-LogToFile', str(log),
               '-PathTracingRrRecoveryValidation', str(out)]
    if args.max_checkpoints:
        command += ['-PathTracingRrRecoveryMaxCheckpoints', str(args.max_checkpoints)]
    report = dict(status='running', command=command,
                  executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    try:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
        try:
            code = child.wait(timeout=300)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
        require_isolation()
        report['exitCode'] = code
        text = log.read_text(encoding='utf-8-sig')
        assert code == 0 and '[RrRecoveryComplete] passed=true' in text, text[-4000:]
        assert '[ERROR]' not in text and '[CORRUPTION]' not in text and '[RrRecoveryFailure]' not in text
        rows = [json.loads(line[len('[RrRecovery] '):]) for line in text.splitlines()
                if line.startswith('[RrRecovery] ')]
        expected = ['baseline'] + [prefix + name for name in FAULTS for prefix in ['armed-', 'rejected-', 'recovered-']]
        expected += ['epoch-cancelled', 'epoch-recovered', 'native', 'nrd-selected', 'rr-restored',
                     'resize-cancelled', 'resized-recovered', 'resize-invalidated', 'resize-restored']
        if args.max_checkpoints:
            expected = expected[:args.max_checkpoints]
        assert [r['case'] for r in rows] == expected
        pinned = []
        by_name = {r['case']: r for r in rows}
        for name in FAULTS:
            if 'recovered-' + name not in by_name:
                continue
            before_meta, before = read_buffer(out / (name + '-before.ptbuf'))
            after_meta, after = read_buffer(out / (name + '-after.ptbuf'))
            assert before.shape == after.shape and np.isfinite(before[..., :3]).all()
            assert before.tobytes() == after.tobytes(), f'{name}: rejected SDK work modified retained image'
            armed, rejected, recovered = [by_name[p + name] for p in ['armed-', 'rejected-', 'recovered-']]
            assert rejected['completed'] == armed['completed'] and rejected['published'] == armed['published']
            assert rejected['dirty'] and rejected['verified'] and recovered['lastReset'] and not recovered['dirty']
            assert recovered['completed'] == rejected['completed'] + 1
            assert rejected['serial'] == armed['serial'] + 1 and recovered['serial'] == rejected['serial'] + 1
            pinned.append(dict(fault=name, exactRetainedImage=True,
                               rgbSha256=hashlib.sha256(before[..., :3].tobytes()).hexdigest()))
        assert all(r['fence'] == 0 for r in rows)
        if not args.max_checkpoints or args.max_checkpoints == 31:
            last = rows[-1]
            assert last['calls'] == sum(last[k] for k in
                ['completed', 'inputRejected', 'outputRejected', 'sdkFailures', 'cancelled'])
        report.update(status='diagnostic-passed' if args.max_checkpoints and args.max_checkpoints < 31 else 'passed', checkpoints=rows, retainedImages=pinned, d3d12Errors=0)
        print('RR recovery passed:', len(rows), 'checkpoints;', len(pinned), 'retained images unchanged')
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
