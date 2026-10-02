"""Summarize native guide captures and plot camera-axis depth discrepancies."""
import argparse
import csv
import json
from pathlib import Path
import sys
from validate_part1 import write_json, sha
from validate_inputs import ROOT, read_buffer, plane_hits, analyze


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/part5-inputs-final')
    parser.add_argument('--packages', type=Path)
    args = parser.parse_args()
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    import numpy as np
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    output = args.output.resolve()
    report = json.loads((output/'report.json').read_text(encoding='utf-8'))
    if report['failures'] or len(report['records']) != 20:
        raise ValueError('Need the complete 20-run final cohort, retaining all failure reports')
    results = [r['result'] for r in report['records']]
    report['checks'] = dict(
        normalRoughnessPassed=all(r['passed'] for r in results if r['resource'] == 'NormalRoughness'),
        depthImplementationPassed=all(r['shaderDefinitionPassed'] for r in results if r['resource'] == 'ViewZ'),
        albedoImplementationPassed=all(r['shaderDefinitionPassed'] for r in results if r['resource'] == 'Albedo'),
        primaryMaterialMismatchCount=sum(r.get('primaryHitMaterialMismatchCount', 0) for r in results),
        motionHalfPrecisionBoundPassed=all(r['halfPrecisionBoundPassed'] for r in results if r['resource'] == 'MotionVectors'),
        d3d12Errors=sum(r['d3d12Errors'] for r in report['records']))
    with (output/'metrics.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=['name', 'resource', 'maxAbsoluteError', 'meanAbsoluteError', 'tolerance', 'passed',
            'shaderDefinitionMaxError', 'shaderDefinitionPassed', 'halfPrecisionBoundPassed', 'maxBoundNormalizedError'])
        writer.writeheader()
        for record in report['records']:
            writer.writerow(dict(name=record['name'], **{key: record['result'].get(key, '') for key in writer.fieldnames if key != 'name'}))
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), constrained_layout=True)
    for index, scene in enumerate(['input-plane', 'input-shifted', 'input-ortho']):
        meta, values = read_buffer(output/f'{scene}-ViewZ-static.ptbuf')
        image = axes[0, index].imshow(values[::4, ::4, 0], vmin=3, vmax=7, cmap='viridis', origin='upper')
        axes[0, index].set_title(scene+' / stored ViewZ')
        axes[0, index].set_xlabel('pixel x / 4')
        axes[0, index].set_ylabel('pixel y / 4')
        xs = np.arange(16, meta['width']-16, 4)
        ys = np.full(len(xs), meta['height']//2)
        world, _, _ = plane_hits(meta, xs, ys)
        forward = np.asarray(meta['cameraTarget'])-np.asarray(meta['cameraPosition'])
        forward /= np.linalg.norm(forward)
        expected = (world-np.asarray(meta['cameraPosition'])) @ forward
        axes[1, index].plot(xs, values[ys, xs, 0], label='stored')
        axes[1, index].plot(xs, expected, '--', label='camera-axis depth')
        axes[1, index].set_ylim(3, 7)
        axes[1, index].set_xlabel('pixel x, center row')
        axes[1, index].set_ylabel('world units')
        axes[1, index].grid(alpha=.3)
        axes[1, index].legend()
    fig.colorbar(image, ax=list(axes[0]), label='world units', shrink=.8)
    fig.savefig(output/'view-z-validation.png', dpi=160)
    fig.savefig(output/'view-z-validation.svg')
    plt.close(fig)
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.5), constrained_layout=True)
    for index, scene in enumerate(['input-plane', 'input-shifted', 'input-ortho']):
        meta, values = read_buffer(output/f'{scene}-MotionVectors-moving.ptbuf')
        xs, ys = np.meshgrid(np.arange(60, meta['width']-60, 120), np.arange(60, meta['height']-60, 120))
        motion = values[ys, xs] * [meta['width']/2, -meta['height']/2]
        axes[index].quiver(xs, ys, motion[:, :, 0], motion[:, :, 1], angles='xy', scale_units='xy', scale=1, width=.003)
        axes[index].set_xlim(0, meta['width'])
        axes[index].set_ylim(meta['height'], 0)
        axes[index].set_aspect('equal')
        axes[index].set_title(scene+' / previous-current pixels')
        axes[index].set_xlabel('pixel x')
        axes[index].set_ylabel('pixel y')
    fig.savefig(output/'motion-validation.png', dpi=160)
    fig.savefig(output/'motion-validation.svg')
    plt.close(fig)
    report['artifacts'] = [{ 'path': str(p.relative_to(ROOT)), 'sha256': sha(p)} for p in sorted(output.iterdir())
        if p.is_file() and p.name != 'summary.json']
    report['status'] = 'done'
    write_json(output/'summary.json', report)
    write_json(ROOT/'doc/branch/feature/path-tracing-validation-results/part-5-summary.json', report)
    print(json.dumps(report['checks'], indent=2))


if __name__ == '__main__':
    main()
