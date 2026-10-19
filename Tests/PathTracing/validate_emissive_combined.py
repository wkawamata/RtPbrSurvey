"""Check emissive/point/environment additivity and joint MIS mean agreement."""
import argparse
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

import numpy as np
from compare_hdr import capture
from validate_emissive import ROOT, ROI, fixture
from validate_part1 import sha, write_json
from validate_transport import paired_agreement


def make_fixture(output):
    scene, preset = fixture(output)
    scene.update(sceneId='emissive-combined', name='PT Combined Lighting')
    preset['renderingPath'] = 2
    preset['lighting'].update(iblIntensity=.2, lights=[dict(id=1, name='Cool point', type='point',
        enabled=True, color=[.2, .5, 1], intensity=4, position=[-2, 2, -1],
        direction=[0, -1, 0], range=20, innerCone=15, outerCone=30)])
    set_components(preset, 'combined', 2)
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    return scene, preset


def set_components(preset, component, mode):
    if component not in ['emissive', 'direct', 'environment', 'combined', 'off']:
        raise ValueError('Unknown lighting component')
    preset['pathTracing'].update(emissiveSamplingMode=mode, environmentSamplingMode=5,
        emissiveEnabled=component in ['emissive', 'combined'],
        directLightingEnabled=component in ['direct', 'combined'],
        environmentEnabled=component in ['environment', 'combined'])


def compare_rgb(reference, candidate):
    reference = np.asarray(reference, dtype=float)
    candidate = np.asarray(candidate, dtype=float)
    if reference.shape != candidate.shape or reference.ndim != 2 or reference.shape[1] != 3:
        raise ValueError('Require paired RGB seed observations')
    return dict(mean=paired_agreement(reference.mean(axis=1).tolist(), candidate.mean(axis=1).tolist()),
        channels=[paired_agreement(reference[:, channel].tolist(), candidate[:, channel].tolist())
            for channel in range(3)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--samples', type=int, default=128)
    parser.add_argument('--seeds', type=int, nargs='+', default=[11, 23, 37, 53])
    args = parser.parse_args()
    if not 1 <= args.samples <= 4096 or len(set(args.seeds)) < 4 or len(set(args.seeds)) != len(args.seeds):
        parser.error('Require 1-4096 samples and at least four unique seeds')
    if any(seed < 0 or seed > 0xffffffff for seed in args.seeds):
        parser.error('Seeds must be uint32')
    args.exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    _, preset = make_fixture(output)
    report = dict(schemaVersion=1, status='running', records=[], comparisons=[], samples=args.samples,
        seeds=args.seeds, roi=ROI, sceneSha256=sha(output/'scene.json'), emitterSha256=sha(output/'emitter.gltf'),
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executable=str(args.exe), executableSha256=sha(args.exe),
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Shaders/PathTracingSampling.hlsli', 'Shaders/EmissiveTriangleSampling.hlsli',
            'Tests/PathTracing/validate_emissive.py', 'Tests/PathTracing/validate_emissive_combined.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip(),
        limitation='Two-segment opaque receiver/rectangle fixture; constant environment and one point light. '
            'Additivity/technique agreement is not an independent absolute transport oracle.')
    request = SimpleNamespace(root=ROOT, exe=args.exe, scene_file=output/'scene.json',
        render_preset=output/'preset.json', output=output, timeout=300, roi=ROI)

    def run(component, mode, env_mode=5, seeds=None):
        set_components(preset, component, mode)
        preset['pathTracing']['environmentSamplingMode'] = env_mode
        write_json(output/'preset.json', preset)
        channels = []
        for seed in args.seeds if seeds is None else seeds:
            label = f'{component}-m{mode}-env{env_mode}'
            record, pixels = capture(request, env_mode, seed, args.samples, f'{label}-seed-{seed}')
            diagnostic = record['diagnostics']
            expected = preset['pathTracing']
            if (diagnostic['emissiveTriangleCount'] != 2 or diagnostic['emissiveTableStatus'] != 'ready' or
                    diagnostic['emissiveSamplingMode'] != mode or diagnostic['maxBounces'] != 2 or
                    diagnostic['russianRoulette'] or tuple(record['dimensions']) != (1920, 1080) or
                    any(diagnostic[field] != expected[field] for field in
                        ['emissiveEnabled', 'directLightingEnabled', 'environmentEnabled'])):
                raise ValueError('Unexpected combined-lighting integration settings')
            rgb = np.asarray(pixels).reshape(-1, 3).mean(axis=0)
            record.update(label=label, component=component, emissiveSamplingMode=mode,
                environmentSamplingMode=env_mode, mean=float(rgb.mean()), rgb=rgb.tolist(),
                maximum=float(np.max(np.abs(pixels))), presetSha256=sha(output/'preset.json'))
            channels.append(rgb)
            report['records'].append(record)
            write_json(output/'report.json', report)
        return np.asarray(channels)

    try:
        emission = {mode: run('emissive', mode) for mode in [0, 1, 2]}
        direct = run('direct', 2)
        environment = run('environment', 2)
        if min(float(direct.mean()), float(environment.mean()), float(emission[2].mean())) < 1e-4:
            raise ValueError('An isolated lighting component is too dark')
        combined = {mode: run('combined', mode) for mode in [0, 1, 2]}
        for mode in [0, 1, 2]:
            report['comparisons'].append(dict(case=f'additivity-mode-{mode}',
                **compare_rgb(emission[mode]+direct+environment, combined[mode])))
        for mode in [1, 2]:
            report['comparisons'].append(dict(case=f'combined-bsdf-vs-mode-{mode}',
                **compare_rgb(combined[0], combined[mode])))
        bsdf_environment = run('combined', 0, 1)
        report['comparisons'].append(dict(case='joint-mis-vs-pure-bsdf',
            **compare_rgb(bsdf_environment, combined[2])))
        run('off', 2, seeds=args.seeds[:1])
        report['offMaximum'] = report['records'][-1]['maximum']
        report['componentMeans'] = dict(emissive=float(emission[2].mean()), direct=float(direct.mean()),
            environment=float(environment.mean()), combined=float(combined[2].mean()),
            sum=float((emission[2]+direct+environment).mean()))
        statuses = [value['status'] for comparison in report['comparisons']
            for value in [comparison['mean']]+comparison['channels']]
        report['status'] = ('failed' if 'failed' in statuses or report['offMaximum'] >= 1e-7 else
            'inconclusive' if 'inconclusive' in statuses else 'passed')
    except Exception as error:
        report.update(status='failed', failure=str(error))
    set_components(preset, 'combined', 2)
    write_json(output/'preset.json', preset)
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failure=report.get('failure'),
        componentMeans=report.get('componentMeans'), comparisons=report['comparisons'])), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
