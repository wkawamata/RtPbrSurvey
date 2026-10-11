"""Measure sample-count convergence using per-pixel inter-seed variance."""
import argparse
import json
from pathlib import Path
import subprocess
from types import SimpleNamespace

import numpy as np
from compare_hdr import capture
from validate_emissive import ROOT, ROI, fixture, reference
from validate_part1 import sha, write_json
from validate_transport import reference_agreement, paired_agreement


def seed_variance(images):
    images = np.asarray(images, dtype=float)
    if images.ndim != 2 or images.shape[0] < 4 or not np.all(np.isfinite(images)):
        raise ValueError('Require four finite equal-sized seed images')
    return float(np.var(images, axis=0, ddof=1).mean())


def assess_curve(samples, variances):
    if len(samples) != len(variances) or len(samples) < 3 or any(v <= 1e-14 for v in variances):
        raise ValueError('Require three nonzero variance measurements')
    if any(b < a*4 for a, b in zip(samples, samples[1:])):
        raise ValueError('Sample counts must grow by at least four')
    ratios = [b/a for a, b in zip(variances, variances[1:])]
    slope = float(np.polyfit(np.log(samples), np.log(variances), 1)[0])
    return dict(status='passed' if max(ratios) <= .65 and variances[-1]/variances[0] <= .25
        and slope <= -.5 else 'failed', variances=variances, adjacentRatios=ratios,
        finalToInitialRatio=variances[-1]/variances[0], logVarianceSlope=slope,
        policy=dict(maxAdjacentRatio=.65, maxFinalToInitialRatio=.25, maxSlope=-.5))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--samples', type=int, nargs='+', default=[16, 64, 256])
    parser.add_argument('--seeds', type=int, nargs='+', default=[11, 23, 37, 53])
    args = parser.parse_args()
    if (len(args.samples) < 3 or any(s < 1 or s > 4096 for s in args.samples) or
            any(b < a*4 for a, b in zip(args.samples, args.samples[1:])) or
            len(set(args.seeds)) < 4 or len(set(args.seeds)) != len(args.seeds) or
            any(seed < 0 or seed > 0xffffffff for seed in args.seeds)):
        parser.error('Require three sample counts increasing >=4x and four unique uint32 seeds')
    args.exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    scene, preset = fixture(output)
    preset['renderingPath'] = 2
    preset['pathTracing']['environmentSamplingMode'] = 0
    oracle = reference(scene)
    report = dict(schemaVersion=1, status='running', samples=args.samples, seeds=args.seeds,
        roi=ROI, reference=oracle, records=[], cohorts=[], curves=[], pairedComparisons=[],
        commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        executable=str(args.exe), executableSha256=sha(args.exe), sceneSha256=sha(output/'scene.json'),
        emitterSha256=sha(output/'emitter.gltf'),
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Tests/PathTracing/validate_emissive.py', 'Tests/PathTracing/validate_emissive_convergence.py']},
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version',
            '--format=csv,noheader'], text=True).strip(),
        limitation='Inter-seed ROI variance measures stochastic noise, not deterministic bias. '
            'Mean reference uses nine representative ROI positions; no denoiser, RR or motion.')
    request = SimpleNamespace(root=ROOT, exe=args.exe, scene_file=output/'scene.json',
        render_preset=output/'preset.json', output=output, timeout=300, roi=ROI)
    means = {}
    try:
        repeat_hash = None
        for mode in [0, 1, 2]:
            preset['pathTracing']['emissiveSamplingMode'] = mode
            write_json(output/'preset.json', preset)
            variances = []
            for samples in args.samples:
                images = []
                rgb_means = []
                for seed in args.seeds:
                    record, pixels = capture(request, 0, seed, samples, f'mode-{mode}-spp-{samples}-seed-{seed}')
                    diagnostic = record['diagnostics']
                    if (diagnostic['emissiveTriangleCount'] != 2 or diagnostic['emissiveTableStatus'] != 'ready' or
                            diagnostic['emissiveSamplingMode'] != mode or diagnostic['maxBounces'] != 2 or
                            diagnostic['russianRoulette'] or diagnostic['environmentEnabled'] or
                            diagnostic['directLightingEnabled'] or not diagnostic['emissiveEnabled'] or
                            tuple(record['dimensions']) != (1920, 1080)):
                        raise ValueError('Unexpected convergence settings')
                    image = np.asarray(pixels, dtype=float)
                    rgb = image.reshape(-1, 3).mean(axis=0)
                    record.update(emissiveSamplingMode=mode, mean=float(rgb.mean()), rgb=rgb.tolist(),
                        presetSha256=sha(output/'preset.json'))
                    report['records'].append(record)
                    images.append(image)
                    rgb_means.append(rgb)
                    if mode == 2 and samples == args.samples[-1] and seed == args.seeds[0]:
                        repeat_hash = record['sha256']
                    write_json(output/'report.json', report)
                cohort_means = [float(rgb.mean()) for rgb in rgb_means]
                means[mode, samples] = cohort_means
                variance = seed_variance(images)
                variances.append(variance)
                report['cohorts'].append(dict(mode=mode, samples=samples, seedVariance=variance,
                    meanAgreement=reference_agreement(cohort_means, oracle),
                    channelAgreements=[reference_agreement([float(rgb[c]) for rgb in rgb_means],
                        dict(oracle, mean=oracle['rgb'][c])) for c in range(3)]))
            report['curves'].append(dict(mode=mode, **assess_curve(args.samples, variances)))
        for samples in args.samples:
            for mode in [1, 2]:
                report['pairedComparisons'].append(dict(samples=samples, mode=mode,
                    **paired_agreement(means[0, samples], means[mode, samples])))
        record, _ = capture(request, 0, args.seeds[0], args.samples[-1], 'repeat-mis')
        report['repeat'] = dict(sha256=record['sha256'], originalSha256=repeat_hash,
            passed=record['sha256'] == repeat_hash)
        states = [curve['status'] for curve in report['curves']]+[
            entry['status'] for cohort in report['cohorts']
            for entry in [cohort['meanAgreement']]+cohort['channelAgreements']]+[
            comparison['status'] for comparison in report['pairedComparisons']]
        report['status'] = ('failed' if 'failed' in states or not report['repeat']['passed'] else
            'inconclusive' if 'inconclusive' in states else 'passed')
    except Exception as error:
        report.update(status='failed', failure=str(error))
    write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], failure=report.get('failure'),
        curves=report['curves'], repeat=report.get('repeat'))), flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
