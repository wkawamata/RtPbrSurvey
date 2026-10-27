"""Audit RR distance lineage; numeric success never qualifies native evaluation."""
import argparse
import json
from pathlib import Path

import numpy as np

from validate_denoiser_distances import capture
from validate_part1 import sha, write_json
from validate_secondary_geometry import RESOURCES as GEOMETRY, assess as assess_geometry


RESOURCES = GEOMETRY + ('DenoiserSpecularDirection', 'DenoiserSpecularDistance', 'PhysicalSpecular')


def assess(buffers, scene):
    geometry = assess_geometry({name: buffers[name] for name in GEOMETRY}, scene)
    meta = buffers['Albedo'][0]
    common = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    for name, (other, values) in buffers.items():
        if ({key: value for key, value in other.items() if key not in ('resource', 'format')} != common
                or other['resource'] != 'PathTracing.'+name
                or other['format'] != (10 if name == 'Albedo' else 2)
                or values.shape != (meta['height'], meta['width'], 4)
                or not np.isfinite(values).all()):
            raise ValueError('Lineage cohort mismatch: '+name)
    secondary = buffers['SecondaryRayData'][1]
    continuation = buffers['PrimaryContinuation'][1]
    probe = buffers['DenoiserSpecularDirection'][1]
    distance = buffers['DenoiserSpecularDistance'][1]
    physical = buffers['PhysicalSpecular'][1]
    primary = buffers['Albedo'][1][..., 3] == 1
    traced = np.isin(secondary[..., 2], [1, 2])
    comparable = primary & traced & (continuation[..., 3] > 0) & (probe[..., 3] > 0)
    different = comparable & (np.linalg.norm(continuation[..., :3]-probe[..., :3], axis=-1) > 1e-4)
    diffuse_with_specular = traced & (secondary[..., 1] == 1) & (physical[..., :3].max(axis=-1) > 0)
    checks = dict(geometry=geometry['passed'], independentDirectionObserved=bool(different.sum() >= 100),
                  diffuseProposalCarriesSpecular=bool(diffuse_with_specular.sum() >= 100),
                  primaryCoverage=bool(primary.sum() >= 100),
                  probeStates=bool(np.isin(distance[..., 1], [0, 1, 2, 3, 4]).all()))
    table = []
    for actual in (0, 1, 2):
        table.append([int(((secondary[..., 2] == actual) & (distance[..., 1] == auxiliary)).sum())
                      for auxiliary in range(5)])
    return dict(passed=all(checks.values()), checks=checks, geometry=geometry,
                comparableDirections=int(comparable.sum()), differentDirections=int(different.sum()),
                diffuseProposalWithNonzeroPhysicalSpecular=int(diffuse_with_specular.sum()),
                actualStateRows=[0, 1, 2], probeStateColumns=list(range(5)), stateCrossTable=table,
                nativeEvaluationQualified=False,
                decision='Independent NRD probe is not the actual radiance continuation. Do not promote it to RR hit distance.',
                limitation='Finite-plane fixture, one pixel-center sample, first continuation only. Actual continuation distance is not yet an RR contract.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    source, output, exe = args.source.resolve(strict=True), args.output.resolve(), args.exe.resolve(strict=True)
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Use a separate empty output directory')
    scene = json.loads((source/'scene.json').read_text())
    preset = json.loads((source/'preset.json').read_text())
    pt = preset['pathTracing']
    if (pt['samplesPerFrame'] != 1 or not pt['pixelCenterPrimary'] or not pt['denoiserDistanceDiagnostics']
            or pt['maxBounces'] != 2 or pt['directLightingEnabled'] or pt['emissiveEnabled']
            or pt['environmentSamplingMode'] != 5 or pt['russianRouletteEnabled']):
        parser.error('Requires the two-bounce BSDF-only environment fixture')
    write_json(output/'scene.json', scene)
    write_json(output/'preset.json', preset)
    (output/'validation-script.py').write_bytes(Path(__file__).read_bytes())
    report = dict(status='running', records=[], failures=[], exeSha256=sha(exe),
                  producerShaderSha256=sha(exe.parent/'shaders_PathTracing_CSMain.cso'),
                  scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'),
                  presetSha256=sha(output/'preset.json'), nativeEvaluationQualified=False)
    try:
        buffers = {}
        for name in RESOURCES:
            buffers[name], record = capture(exe, output, name, output/'preset.json', name)
            report['records'].append(record)
            write_json(output/'report.json', report)
            print('Captured '+name, flush=True)
        report['assessment'] = assess(buffers, scene)
        if not report['assessment']['passed']:
            raise ValueError('Distance lineage audit failed')
    except Exception as error:
        report['failures'].append(str(error))
    report['status'] = 'failed' if report['failures'] else 'passed'
    write_json(output/'report.json', report)
    print(json.dumps(report.get('assessment', report['failures'])), flush=True)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
