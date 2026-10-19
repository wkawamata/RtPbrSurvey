"""Validate emissive visibility and deterministic estimator fallback controls."""
import argparse
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

import numpy as np
from compare_hdr import capture
from validate_emissive import ROOT, ROI, fixture, blocked_scene
from validate_part1 import sha, write_json


CASES = ['baseline', 'backface', 'beyond', 'empty', 'shadow-off']


def make_fixture(output, case):
    scene, preset = fixture(output)
    if case == 'backface':
        scene['nodes'][1].update(rotation=[1, 0, 0, 0], translation=[0, 6, 0])
    elif case == 'beyond':
        scene = blocked_scene(scene)
        scene['nodes'][-1]['translation'] = [0, 4.5, 0]
    elif case == 'empty':
        scene['nodes'] = scene['nodes'][:1]
        scene['assets'] = []
        preset['pathTracing']['environmentEnabled'] = True
    elif case == 'shadow-off':
        preset['shadow']['enabled'] = False
    elif case != 'baseline':
        raise ValueError('Unknown control case')
    scene.update(sceneId='emissive-control-'+case, name='PT Emissive '+case)
    preset['renderingPath'] = 2
    preset['pathTracing'].update(emissiveSamplingMode=1, environmentSamplingMode=1 if case == 'empty' else 0)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    return scene, preset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--samples', type=int, default=64)
    parser.add_argument('--seed', type=int, default=11)
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or not 0 <= args.seed <= 0xffffffff:
        parser.error('Require 1-4096 samples and a uint32 seed')
    args.exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(schemaVersion=1, status='running', cases=[], samples=args.samples, seed=args.seed,
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(args.exe), executable=str(args.exe),
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Tests/PathTracing/validate_emissive_controls.py', 'Tests/PathTracing/validate_emissive.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip(),
        limitation='One fixed seed, 64x64 receiver ROI; equality controls are not convergence tests.')
    baseline = {}
    try:
        for case in CASES:
            folder = output/case
            folder.mkdir()
            scene, preset = make_fixture(folder, case)
            result = dict(case=case, records=[], checks=[], sceneSha256=sha(folder/'scene.json'))
            request = SimpleNamespace(root=ROOT, exe=args.exe, scene_file=folder/'scene.json',
                render_preset=folder/'preset.json', output=folder, timeout=300, roi=ROI)
            for mode in [0, 1, 2]:
                preset['pathTracing']['emissiveSamplingMode'] = mode
                write_json(folder/'preset.json', preset)
                record, pixels = capture(request, 1 if case == 'empty' else 0,
                    args.seed, args.samples, f'mode-{mode}')
                diagnostic = record['diagnostics']
                if (diagnostic['emissiveTriangleCount'] != (0 if case == 'empty' else 2) or
                        diagnostic['emissiveTableStatus'] != 'ready' or
                        diagnostic['emissiveSamplingMode'] != mode or
                        diagnostic['environmentEnabled'] != (case == 'empty') or
                        not diagnostic['emissiveEnabled'] or diagnostic['directLightingEnabled'] or
                        diagnostic['maxBounces'] != 2 or tuple(record['dimensions']) != (1920, 1080)):
                    raise ValueError('Unexpected settings or emitter table: '+case)
                record.update(emissiveSamplingMode=mode, mean=float(np.mean(pixels)),
                    maximum=float(np.max(np.abs(pixels))), presetSha256=sha(folder/'preset.json'))
                result['records'].append(record)
                if case == 'baseline':
                    baseline[mode] = record['sha256']
                    passed = record['mean'] > .01
                elif case == 'backface':
                    passed = record['maximum'] < 1e-7
                elif case == 'beyond':
                    passed = record['sha256'] == baseline[mode]
                else:
                    passed = record['mean'] > .01 and record['sha256'] == result['records'][0]['sha256']
                result['checks'].append(dict(mode=mode, passed=bool(passed)))
            result['status'] = 'passed' if all(c['passed'] for c in result['checks']) else 'failed'
            report['cases'].append(result)
            preset['pathTracing']['emissiveSamplingMode'] = 1
            write_json(folder/'preset.json', preset)
            write_json(output/'report.json', report)
        report['status'] = 'passed' if all(c['status'] == 'passed' for c in report['cases']) else 'failed'
    except Exception as error:
        report.update(status='failed', failure=str(error))
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failure=report.get('failure'),
        cases=[dict(case=c['case'], status=c['status']) for c in report['cases']])), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
