"""Measure deep emissive transport and RR mean preservation across BSDF/NEE/MIS."""
import argparse
import copy
import json
import math
from pathlib import Path
import statistics
import subprocess
from types import SimpleNamespace

import numpy as np
from compare_hdr import capture
from validate_emissive import ROOT, ROI, fixture
from validate_part1 import sha, write_json
from validate_transport import room_fixture, paired_agreement


def make_fixture(output):
    emitter_scene, _ = fixture(output)
    scene, preset = room_fixture()
    scene.update(sceneId='emissive-deep-rr', name='PT Deep Emissive RR',
        assets=emitter_scene['assets'], renderPreset='preset.json')
    scene['nodes'].append(copy.deepcopy(emitter_scene['nodes'][-1]))
    preset['renderingPath'] = 2
    preset['lighting'].update(lights=[], skyboxEnabled=False, diffuseIblEnabled=False,
        specularIblEnabled=False, emissiveEnabled=True)
    preset['pathTracing'].update(maxBounces=8, debugOutput=3, emissiveSamplingMode=2,
        directLightingEnabled=False, environmentEnabled=False, environmentSamplingMode=0,
        emissiveEnabled=True, russianRouletteEnabled=False)
    preset['shadow']['enabled'] = True
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    return scene, preset


def deep_contribution(shallow, deep):
    if len(shallow) != len(deep) or len(deep) < 4:
        raise ValueError('At least four paired seeds are required')
    delta = [b-a for a, b in zip(shallow, deep)]
    mean = statistics.mean(delta)
    uncertainty = 4*statistics.stdev(delta)/math.sqrt(len(delta))
    return dict(twoBounceMean=statistics.mean(shallow), eightBounceMean=statistics.mean(deep),
        contribution=mean, fourStandardError=uncertainty, passed=mean > uncertainty+1e-4)


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
    report = dict(schemaVersion=1, status='running', records=[], comparisons=[],
        samples=args.samples, seeds=args.seeds, roi=ROI,
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executableSha256=sha(args.exe), executable=str(args.exe),
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Shaders/PathTracingSampling.hlsli', 'Shaders/EmissiveTriangleSampling.hlsli',
            'Tests/PathTracing/validate_emissive_rr.py', 'Tests/PathTracing/validate_emissive.py',
            'Tests/PathTracing/validate_transport.py']}, sceneSha256=sha(output/'scene.json'),
        emitterSha256=sha(output/'emitter.gltf'),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip(),
        limitation='Relative mean preservation in one enclosed diffuse room, not an absolute deep-transport oracle.')
    request = SimpleNamespace(root=ROOT, exe=args.exe, scene_file=output/'scene.json',
        render_preset=output/'preset.json', output=output, timeout=300, roi=ROI)

    def run(mode, rr, bounces, label, seeds):
        preset['pathTracing'].update(emissiveSamplingMode=mode, russianRouletteEnabled=rr,
            maxBounces=bounces)
        write_json(output/'preset.json', preset)
        means = []
        hashes = []
        for seed in seeds:
            record, pixels = capture(request, 0, seed, args.samples, f'{label}-seed-{seed}')
            diagnostic = record['diagnostics']
            if (diagnostic['emissiveTriangleCount'] != 2 or diagnostic['emissiveTableStatus'] != 'ready' or
                    diagnostic['emissiveSamplingMode'] != mode or diagnostic['maxBounces'] != bounces or
                    diagnostic['russianRoulette'] != rr or diagnostic['environmentEnabled'] or
                    diagnostic['directLightingEnabled'] or
                    diagnostic['emissiveEnabled'] != preset['pathTracing']['emissiveEnabled'] or
                    tuple(record['dimensions']) != (1920, 1080)):
                raise ValueError('Unexpected deep transport integration settings')
            rgb = np.asarray(pixels).reshape(-1, 3).mean(axis=0)
            record.update(label=label, emissiveSamplingMode=mode, russianRouletteEnabled=rr,
                maxBounces=bounces, mean=float(rgb.mean()), rgb=rgb.tolist(),
                maximum=float(np.max(np.abs(pixels))), presetSha256=sha(output/'preset.json'))
            report['records'].append(record)
            means.append(record['mean'])
            hashes.append(record['sha256'])
            write_json(output/'report.json', report)
        return means, hashes

    try:
        means = {}
        hashes = {}
        for mode in [0, 1, 2]:
            for rr in [False, True]:
                means[mode, rr], hashes[mode, rr] = run(mode, rr, 8,
                    f'mode-{mode}-rr-{int(rr)}-b8', args.seeds)
        for mode in [0, 1, 2]:
            report['comparisons'].append(dict(case=f'mode-{mode}-rr',
                **paired_agreement(means[mode, False], means[mode, True])))
        for rr in [False, True]:
            for mode in [1, 2]:
                report['comparisons'].append(dict(case=f'rr-{int(rr)}-bsdf-vs-mode-{mode}',
                    **paired_agreement(means[0, rr], means[mode, rr])))
        shallow, _ = run(2, False, 2, 'mode-2-rr-0-b2', args.seeds)
        report['deepContribution'] = deep_contribution(shallow, means[2, False])
        report['rrChangesImages'] = all(hashes[mode, False] != hashes[mode, True] for mode in [0, 1, 2])
        preset['pathTracing']['emissiveEnabled'] = False
        run(2, True, 8, 'emission-off', args.seeds[:1])
        report['offMaximum'] = report['records'][-1]['maximum']
        statuses = [c['status'] for c in report['comparisons']]
        report['status'] = ('failed' if 'failed' in statuses or not report['deepContribution']['passed'] or
            not report['rrChangesImages'] or report['offMaximum'] >= 1e-7 else
            'inconclusive' if 'inconclusive' in statuses else 'passed')
    except Exception as error:
        report.update(status='failed', failure=str(error))
    preset['pathTracing'].update(emissiveSamplingMode=2, russianRouletteEnabled=True,
        emissiveEnabled=True, maxBounces=8)
    write_json(output/'preset.json', preset)
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failure=report.get('failure'),
        comparisons=report['comparisons'], deepContribution=report.get('deepContribution'))), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
