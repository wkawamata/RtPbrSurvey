"""Necessary neighbor coverage only; not an NRD reconstruction or dispatch approval."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_secondary_geometry import planes_from_scene


def witnesses(seeds, labels, radius, normal_roughness=None):
    if seeds.shape != labels.shape or seeds.ndim != 2 or radius not in (1, 2):
        raise ValueError('Requires matching 2D inputs and radius 1 or 2')
    result = np.zeros(seeds.shape, dtype=bool)
    if normal_roughness is not None:
        if (normal_roughness.shape != seeds.shape+(4,) or not np.isfinite(normal_roughness).all() or
                ((normal_roughness[..., 3] < 0) | (normal_roughness[..., 3] > 1)).any()):
            raise ValueError('Invalid normal/roughness guide')
        lengths = np.linalg.norm(normal_roughness[..., :3], axis=-1)
        valid = abs(lengths-1) <= .005
        normals = normal_roughness[..., :3]/np.maximum(lengths[..., None], 1e-12)
    height, width = seeds.shape
    for dy in range(-radius, radius+1):
        for dx in range(-radius, radius+1):
            y0, y1 = max(0, -dy), min(height, height-dy)
            x0, x1 = max(0, -dx), min(width, width-dx)
            if y0 >= y1 or x0 >= x1:
                continue
            target = np.s_[y0:y1, x0:x1]
            source = np.s_[y0+dy:y1+dy, x0+dx:x1+dx]
            compatible = (labels[target] >= 0) & (labels[target] == labels[source])
            if normal_roughness is not None:
                compatible &= (valid[target] & valid[source] &
                    (np.sum(normals[target]*normals[source], axis=-1) >= .99) &
                    (abs(normal_roughness[target][..., 3]-normal_roughness[source][..., 3]) <= .02))
            result[target] |= seeds[source] & compatible
    return result


def coverage(missing, seeds, labels, radius, normal_roughness=None):
    if missing.shape != seeds.shape or (missing & seeds).any():
        raise ValueError('Missing and valid seeds must be disjoint and matching')
    found = witnesses(seeds, labels, radius, normal_roughness)
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


def assess_plane_normals(guide, labels, planes):
    if guide.shape != labels.shape+(4,) or not np.isfinite(guide).all():
        raise ValueError('Invalid matched normal guide')
    known = labels >= 0
    if not known.any():
        raise ValueError('No labelled normal samples')
    normals = guide[..., :3][known]
    lengths = np.linalg.norm(normals, axis=-1)
    expected = np.asarray([plane[3] for plane in planes])[labels[known]]
    dots = np.sum(normals*expected, axis=-1)/np.maximum(lengths, 1e-12)
    if (abs(lengths-1) > .005).any() or (dots < .99).any():
        raise ValueError('Normal guide disagrees with independent plane normals')
    return dict(checkedPixels=int(known.sum()), minimumNormalDot=float(dots.min()),
        maximumUnitLengthError=float(abs(lengths-1).max()),
        excludedUnlabelledPixels=int((~known).sum()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-normal-guide', action='store_true')
    args = parser.parse_args()
    folder = args.input.resolve(strict=True)
    report_path = folder/'report.json'
    source = json.loads(report_path.read_text())
    scene_path = folder/'scene.json'
    if (source.get('status') != 'passed' or not source.get('assessment', {}).get('passed') or
            source.get('sceneSha256') != sha(scene_path)):
        parser.error('Requires a passing, unchanged geometry cohort')
    names = ('DenoiserPrimaryPosition', 'DenoiserDiffuseDistance', 'DenoiserSpecularDistance')
    if args.require_normal_guide:
        names += ('NormalRoughness',)
    protected = {report_path, scene_path} | set(folder.glob('*.ptbuf'))
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
        if (other['resource'] != 'PathTracing.'+name or other['format'] != (10 if name == 'NormalRoughness' else 2) or
                values.shape != positions.shape or
                {k: v for k, v in other.items() if k not in ('resource', 'format')} !=
                {k: v for k, v in meta.items() if k not in ('resource', 'format')}):
            parser.error('Resource cohort mismatch')
    planes = planes_from_scene(json.loads(scene_path.read_text()))
    labels = surface_labels(positions, planes)
    result = dict(schemaVersion=1, inputReady=False, runtimeGeometrySnapshotVerified=False,
        sourceReportSha256=sha(report_path), scriptSha256=sha(Path(__file__)), lobes={})
    guide = buffers['NormalRoughness'][1] if args.require_normal_guide else None
    result['normalGuideVerified'] = guide is not None
    if guide is not None:
        result['normalAssessment'] = assess_plane_normals(guide, labels, planes)
    result['normalPolicy'] = dict(minimumDot=.99, maximumRoughnessDelta=.02,
        maximumUnitLengthError=.005, sdkKernelEquivalent=False)
    for lobe in ('Diffuse', 'Specular'):
        data = buffers['Denoiser'+lobe+'Distance'][1]
        missing = (data[..., 1] == 3) & (positions[..., 3] == 1)
        hit = (data[..., 1] == 1) & (data[..., 0] > 0)
        miss = (data[..., 1] == 2) & (data[..., 0] == 0) & (data[..., 2] == meta['rayTMax'])
        result['lobes'][lobe] = {str(2*r+1)+'x'+str(2*r+1): dict(
            hitSeedsOnly=coverage(missing, hit, labels, r, guide),
            hypotheticalVerifiedEnvironmentMissSeeds=coverage(missing, hit | miss, labels, r, guide)) for r in (1, 2)}
    result['limitation'] = ('Same-plane neighbor existence is necessary, not sufficient. SDK reconstruction '
        'weights, range/snapshot proof, material identity and demodulation remain unvalidated. '
        'Unknown surface labels remain excluded even with compatible normals; normal thresholds are audit-only.')
    write_json(args.output, result)
    print(json.dumps(result['lobes']))


if __name__ == '__main__':
    main()
