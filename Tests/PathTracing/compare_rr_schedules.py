"""Compare RR schedules at the same 16 verified productions and identical input signals."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from measure_rr_release import require_isolation
from validate_inputs import read_buffer

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
    report = dict(status='running', executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(), runs=[], pairs=[])
    try:
        for scene in ['WarmGlassEdges', 'PathTracingValidation/rr-quality']:
            for moving in [False, True]:
                pair = []
                for schedule in ['safe-serial', 'chained']:
                    require_isolation()
                    name = scene.replace('/', '-') + ('-camera-' if moving else '-static-') + schedule
                    folder = out / name
                    folder.mkdir()
                    command = [str(exe), '-SceneFile', str(ROOT/'Assets/Scenes'/scene/'scene.json'),
                               '-WindowSize', '640', '360', '-EnableExperimentalNativeRayReconstruction',
                               '-PathTracingRrThinExperiment', '-PathTracingRrSmoothVolumeExperiment',
                               '-PathTracingRrTransmissionMotionExperiment', '-PathTracingRrSchedule', schedule,
                               '-PathTracingRrComparisonCapture', str(folder), '-LogToFile', str(folder/'d3d12.log')]
                    if moving:
                        command.append('-PathTracingRrComparisonCamera')
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
                    text = (folder/'d3d12.log').read_text(encoding='utf-8-sig')
                    assert code == 0 and '[RrComparisonComplete] outputs=16 calls=16 reset=1' in text, text[-2500:]
                    assert not any(s in text for s in ['[ERROR]', '[CORRUPTION]', '[RrComparisonFailure]'])
                    signals = []
                    keys = []
                    for index in range(9):
                        metadata, values = read_buffer(folder/f'signal-{index}.ptbuf')
                        assert np.isfinite(values[..., :3] if index in (0,1,2,3,8) else values).all()
                        signals.append(values)
                        keys.append(metadata['rrRetainedInput'])
                    assert all(k == keys[0] for k in keys)
                    assert keys[0]['frameId'] == keys[0]['publishedFrameId'] == 16
                    report['runs'].append(dict(name=name, command=command, key=keys[0], exitCode=code))
                    pair.append(signals)
                    print(name, 'captured', flush=True)
                reference, actual = pair
                input_parity = [a.tobytes() == b.tobytes() for a,b in zip(reference[:8],actual[:8])]
                a, b = reference[8][..., :3].astype(np.float64), actual[8][..., :3].astype(np.float64)
                rmse = float(np.sqrt(np.mean((a-b)**2)))
                relative = rmse / max(float(np.sqrt(np.mean(a*a))), 1e-8)
                same = bool(np.allclose(a,b,rtol=1e-5,atol=1e-5))
                report['pairs'].append(dict(scene=scene, camera=moving, inputByteParity=input_parity,
                    outputByteParity=reference[8].tobytes()==actual[8].tobytes(),
                    outputRmse=rmse, outputRelativeRmse=relative, outputMaxAbs=float(np.abs(a-b).max()),
                    numericParity=same, tolerance=dict(atol=1e-5,rtol=1e-5)))
                (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
                assert all(input_parity), 'Compared schedules used different numeric inputs'
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == report['executableSha256']
        report['status'] = 'passed' if all(p['numericParity'] for p in report['pairs']) else 'output-difference'
    except Exception as error:
        report.update(status='failed',error=str(error))
        raise
    finally:
        (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(report['status'], 'pairs:', len(report['pairs']))


if __name__ == '__main__':
    main()
