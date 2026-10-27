"""Audit native proposal provenance; this does not produce packed NRD inputs."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json
from validate_secondary_geometry import assess, RESOURCES


def audit(buffers, scene):
    geometry = assess(buffers, scene)
    if not geometry['passed']:
        raise ValueError('Independent secondary geometry assessment failed')
    secondary = buffers['SecondaryRayData'][1]
    surface = buffers['SecondarySurfaceDistance'][1]
    if not all(np.isfinite(values).all() for _, values in buffers.values()):
        raise ValueError('Non-finite input')
    status = secondary[..., 2]
    proposal = secondary[..., 1]
    if not np.isin(status, [0, 1, 2]).all() or not np.isin(proposal, [0, 1, 2]).all():
        raise ValueError('Unknown trace state or proposal')
    traced = status != 0
    if ((proposal[traced] == 0).any() or (proposal[~traced] != 0).any() or
            not np.array_equal(surface[..., 1], status) or
            (surface[..., 0] < 0).any() or
            (surface[..., 0][status != 1] != 0).any() or
            (surface[..., 2:] != 0).any()):
        raise ValueError('Invalid corrected-distance state')
    diffuse = traced & (proposal == 1)
    specular = traced & (proposal == 2)
    count = lambda mask: int(np.count_nonzero(mask))
    return dict(schemaVersion=1, status='passed', nrdDistanceReady=False,
        geometryAssessment=geometry,
        pixelCounts=dict(untraced=count(~traced),
            diffuseProposalHits=count(diffuse & (status == 1)),
            diffuseProposalMisses=count(diffuse & (status == 2)),
            specularNdfProposalHits=count(specular & (status == 1)),
            specularNdfProposalMisses=count(specular & (status == 2))),
        policy=dict(diffuse='Cosine proposal is a reuse candidate only for diffuse. '
            'Specular-selected pixels require a separate cosine distance ray.',
            specular='Native GGX NDF does not implement the chosen VNDF distance baseline. '
            'Trace an independent VNDF distance ray; do not reuse by proposal ID alone.',
            miss='Zero plus explicit miss state is diagnostic only; SDK miss mapping is pending.',
            untraced='No measured segment; do not turn it into a valid zero-distance hit.'),
        limitation='Counts cover the full image; independent geometric validation covers '
            'the fixed planar oracle ROI only. No lobe packing or dispatch is performed.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.input.resolve(strict=True)
    source_report = folder/'report.json'
    source = json.loads(source_report.read_text())
    if source.get('status') != 'passed' or not source.get('assessment', {}).get('passed'):
        parser.error('Input capture cohort must have passed')
    records = source.get('records', [])
    if len(records) != len(RESOURCES) or {r.get('resource') for r in records} != set(RESOURCES):
        parser.error('Input report has missing or duplicate resources')
    for record in records:
        if record.get('sha256') != sha(folder/(record['resource']+'.ptbuf')):
            parser.error('Input capture hash does not match source report')
    scene_path = folder/'scene.json'
    buffers = {name: read_buffer(folder/(name+'.ptbuf')) for name in RESOURCES}
    report = audit(buffers, json.loads(scene_path.read_text()))
    report['scriptSha256'] = sha(Path(__file__))
    report['sourceReportSha256'] = sha(source_report)
    report['sceneSha256'] = sha(scene_path)
    report['resources'] = {name: sha(folder/(name+'.ptbuf')) for name in RESOURCES}
    write_json(args.output, report)
    print(json.dumps(report['pixelCounts']))
    print('NRD distance ready: false (dedicated lobe sampling and miss mapping pending)')


if __name__ == '__main__':
    main()
