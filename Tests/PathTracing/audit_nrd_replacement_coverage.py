"""Necessary neighbor coverage only; not an NRD reconstruction or dispatch approval."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_secondary_geometry import planes_from_scene


def witnesses(seeds, labels, radius):
    if seeds.shape != labels.shape or seeds.ndim != 2 or radius not in (1, 2):
        raise ValueError('Requires matching 2D inputs and radius 1 or 2')
    result = np.zeros(seeds.shape, dtype=bool)
    height, width = seeds.shape
    for dy in range(-radius, radius+1):
        for dx in range(-radius, radius+1):
            y0, y1 = max(0, -dy), min(height, height-dy)
            x0, x1 = max(0, -dx), min(width, width-dx)
            if y0 >= y1 or x0 >= x1:
                continue
            target = np.s_[y0:y1, x0:x1]
            source = np.s_[y0+dy:y1+dy, x0+dx:x1+dx]
            result[target] |= seeds[source] & (labels[target] >= 0) & (labels[target] == labels[source])
    return result


def coverage(missing, seeds, labels, radius):
    if missing.shape != seeds.shape or (missing & seeds).any():
        raise ValueError('Missing and valid seeds must be disjoint and matching')
    found = witnesses(seeds, labels, radius)
    return dict(missing=int(missing.sum()), witnessed=int((missing & found).sum()),
        holes=int((missing & ~found).sum()),
        missingWithoutSurfaceLabel=int((missing & (labels < 0)).sum()))


def surface_labels(position, planes):
    if (position.ndim != 3 or position.shape[-1] != 4 or not np.isfinite(position).all() or
            not np.isin(position[..., 3], [0, 1]).all() or len(planes) > np.iinfo(np.int16).max):
        raise ValueError('Invalid primary positions or plane count')
    shape = position.shape[:2]
    points = position[..., :3].reshape(-1, 3)
    labels = np.full(len(points), -1, dtype=np.int16)
    primary = position[..., 3].ravel() == 1
    tolerance = 1e-4+1e-5*np.maximum(1, abs(points).max(axis=-1))
    for index, (center, axis_x, axis_z, normal, half_width, half_depth) in enumerate(planes):
        local = points-center
        matches = (primary & (abs(local @ normal) <= tolerance) &
            (abs(local @ axis_x) < half_width-.002) & (abs(local @ axis_z) < half_depth-.002))
        empty = labels == -1
        labels[matches & ~empty] = -2
        labels[matches & empty] = index
    return labels.reshape(shape)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.input.resolve(strict=True)
    report_path = folder/'report.json'
    source = json.loads(report_path.read_text())
    scene_path = folder/'scene.json'
    if (source.get('status') != 'passed' or not source.get('assessment', {}).get('passed') or
            source.get('sceneSha256') != sha(scene_path)):
        parser.error('Requires a passing, unchanged geometry cohort')
    names = ('DenoiserPrimaryPosition', 'DenoiserDiffuseDistance', 'DenoiserSpecularDistance')
    protected = {report_path, scene_path} | {folder/(name+'.ptbuf') for name in names}
    if args.output.resolve() in protected:
        parser.error('Output must not replace source evidence')
    buffers = {}
    for name in names:
        records = [r for r in source['records'] if r.get('resource') == name]
        path = folder/(name+'.ptbuf')
        if len(records) != 1 or records[0]['sha256'] != sha(path):
            parser.error('Missing, duplicated or altered resource: '+name)
        buffers[name] = read_buffer(path)
    meta, positions = buffers[names[0]]
    for name, (other, values) in buffers.items():
        if (other['resource'] != 'PathTracing.'+name or other['format'] != 2 or
                values.shape != positions.shape or
                {k: v for k, v in other.items() if k != 'resource'} !=
                {k: v for k, v in meta.items() if k != 'resource'}):
            parser.error('Resource cohort mismatch')
    planes = planes_from_scene(json.loads(scene_path.read_text()))
    labels = surface_labels(positions, planes)
    result = dict(schemaVersion=1, inputReady=False, runtimeGeometrySnapshotVerified=False,
        sourceReportSha256=sha(report_path), scriptSha256=sha(Path(__file__)), lobes={})
    for lobe in ('Diffuse', 'Specular'):
        data = buffers['Denoiser'+lobe+'Distance'][1]
        missing = (data[..., 1] == 3) & (positions[..., 3] == 1)
        hit = (data[..., 1] == 1) & (data[..., 0] > 0)
        miss = (data[..., 1] == 2) & (data[..., 0] == 0) & (data[..., 2] == meta['rayTMax'])
        result['lobes'][lobe] = {str(2*r+1)+'x'+str(2*r+1): dict(
            hitSeedsOnly=coverage(missing, hit, labels, r),
            hypotheticalVerifiedEnvironmentMissSeeds=coverage(missing, hit | miss, labels, r)) for r in (1, 2)}
    result['limitation'] = ('Same-plane neighbor existence is necessary, not sufficient. SDK reconstruction '
        'weights, range/snapshot proof, normals/material boundaries and demodulation remain unvalidated.')
    write_json(args.output, result)
    print(json.dumps(result['lobes']))


if __name__ == '__main__':
    main()
