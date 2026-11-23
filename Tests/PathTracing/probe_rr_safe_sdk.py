"""Prepare/run synthetic numeric RR probes; no image-quality or correlation claim."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

from measure_rr_release import require_isolation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', type=Path)
    parser.add_argument('--placeholder-shader', type=Path)
    parser.add_argument('--boundary-shader', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--run-sdk', action='store_true')
    args = parser.parse_args()
    if args.run_sdk and not all([args.replay, args.placeholder_shader, args.boundary_shader]):
        parser.error('SDK run requires replay and both probe shaders')
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    if args.run_sdk:
        require_isolation()
    out.mkdir(parents=True, exist_ok=True)
    width, height, near, far = 640, 360, .1, 100.
    fov = math.radians(35)
    f = 1 / math.tan(fov / 2)
    x, y, a, b = f/(width/height), f, far/(far-near), -near*far/(far-near)
    identity = [float(i % 5 == 0) for i in range(16)]
    projection = [x,0,0,0, 0,y,0,0, 0,0,a,1, 0,0,b,0]
    inverse = [1/x,0,0,0, 0,1/y,0,0, 0,0,0,1/b, 0,0,1,-a/b]
    source = out / 'input'
    source.mkdir()
    (source / 'frame.bin').write_bytes(struct.pack('<16f2I', *inverse, width, height))
    for slot, channels in enumerate([4,4,1,2,4,4,4,4]):
        (source / f'in{slot}.bin').write_bytes(bytes(width*height*channels*4))
    metadata = dict(numericSafetyProbe=True, distanceSource='numeric-safety-probe', nativeOptIn=True,
                    staticCamera=True, samplesPerFrame=1, primarySampling='pixel-center', frameId=1,
                    sampleStartIndex=0, samplingSignature=7, resetToken=1,
                    worldToView=identity, viewToWorld=identity, viewToClip=projection, clipToView=inverse,
                    viewProjection=projection, previousViewProjection=projection,
                    cameraPosition=[0,0,0], cameraRight=[1,0,0], cameraUp=[0,1,0], cameraForward=[0,0,1],
                    nearZ=near, farZ=far, fovRadians=fov)
    (source / 'native.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    report = dict(status='prepared', fixture='synthetic-numeric-only', runs=[])
    try:
        if args.run_sdk:
            for name, shader in [('placeholder', args.placeholder_shader), ('boundary', args.boundary_shader)]:
                require_isolation()
                destination = out / name
                command = [str(args.replay.resolve()), str(shader.resolve()), str(source),
                           str(destination), '--hardware']
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = subprocess.SW_HIDE
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, startupinfo=startup)
                (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
                if result.returncode:
                    raise RuntimeError(f'{name}: SDK probe failed: {result.stdout} {result.stderr}')
                values = json.loads((destination / 'native-result.json').read_text())
                assert values['numericSafetyProbe'] and values['gpuCompleted'] and values['finiteRgb']
                assert values['d3d12Errors'] == 0 and values['nativeEvaluateCallCount'] == 1
                report['runs'].append(dict(name=name, command=command, result=values))
                require_isolation()
            report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(report['status'], out)


if __name__ == '__main__':
    main()
