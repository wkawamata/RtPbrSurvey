"""Verify multi-spp captures are rejected as NRD quality evidence, not silently evaluated."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_nrd_prepass_sensitivity import load_bridge, command
from validate_nrd_prepass_bridge import check_prepass
from requalify_nrd_glossy import read_evidence
from validate_nrd_glossy_rois import ROOT
from validate_part1 import sha, write_json

RESOURCES = ('SceneColor', 'NrdSceneColor', 'NrdValidity')
FALLBACK = 'Native fallback: requires PT Radiance, 1 sample/frame, pixel center and distance diagnostics'


def assess(buffers):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Incomplete multi-spp gate probe')
    shape = buffers['SceneColor'].shape
    if any(image.shape != shape or not np.isfinite(image).all() for image in buffers.values()):
        raise ValueError('Malformed gate probe images')
    if np.count_nonzero(buffers['NrdValidity']):
        raise ValueError('Unsupported sampling unexpectedly admitted NRD inputs')
    if not np.array_equal(buffers['SceneColor'], buffers['NrdSceneColor']):
        raise ValueError('Unsupported sampling did not produce exact Native fallback')
    return dict(nrdInputValidPixels=0, nativeFallbackExact=True, nrdQualityEvaluated=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    bridge, exe = args.bridge.resolve(strict=True), args.executable.resolve(strict=True)
    output = args.output.resolve()
    if any(output == p or p in output.parents for p in (bridge, exe.parent)) or (
            output.exists() and (not output.is_dir() or any(output.iterdir()))):
        parser.error('Output must be a new separate directory')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = {str(p): sha(p) for p in (exe, Path(__file__), bridge/'report.json',
        Path(__file__).with_name('validate_nrd_prepass_sensitivity.py'))}
    report = dict(schemaVersion=1, status='running', qualityVerdict='not-evaluated',
        artifactSha256=artifacts, freshGpuCaptures=0, records=[], cases=[], failures=[],
        fullIntegrationReady=False, limitation='Capability/guard probe only. 4/16 spp are not NRD quality trials.')
    write_json(output/'report.json', report)
    try:
        scene, preset, target, _, _, _ = load_bridge(bridge, exe)
        for spp in (4, 16):
            folder = output/str(spp)
            changed = copy.deepcopy(preset)
            changed['pathTracing']['samplesPerFrame'] = spp
            write_json(folder/'scene.json', scene)
            write_json(folder/'render-preset.json', changed)
            buffers, common = {}, None
            for name in RESOURCES:
                label = f'spp{spp}-{name}'
                cmd = command(exe, folder, 7, 30, name, 'control', label)
                subprocess.run(cmd, cwd=ROOT, check=True, timeout=180)
                record = dict(label=label, resource=name, samplesPerFrame=spp, command=cmd,
                    sha256=sha(folder/(label+'.ptbuf')), logSha256=sha(folder/(label+'.log')), d3d12Errors=0)
                meta, image, runtime = read_evidence(folder, label, record)
                record.update(metadata=meta, diagnostics=runtime)
                report['records'].append(record)
                report['freshGpuCaptures'] = len(report['records'])
                check_prepass(meta)
                if (meta.get('resource') != 'PathTracing.'+name or meta.get('samplesPerFrame') != spp or
                        meta.get('sampleStartIndex') != spp*30 or meta.get('primarySampling') != 'pixel-center' or
                        meta.get('randomSeed') != 7 or meta.get('nrdPreviewRequested') is not True or
                        meta.get('nrdPresentation') != FALLBACK or meta.get('nrdFailure') != '' or
                        runtime.get('samplesPerFrame') != spp or runtime.get('accumulatedSamples') != spp*30 or
                        runtime.get('nrdPresentation') != FALLBACK or runtime.get('maxBounces') != 4 or
                        runtime.get('environmentSamplingMode') != 7):
                    raise ValueError('Gate probe runtime/sampling identity mismatch')
                for key in ('width', 'height', 'viewProjection', 'inverseViewProjection', 'lensShift'):
                    if meta.get(key) != target[key]:
                        raise ValueError('Gate probe camera changed')
                comparable = {k: v for k, v in meta.items() if k not in ('resource', 'format')}
                if common is not None and comparable != common:
                    raise ValueError('Gate probe resource cohort mismatch')
                common = comparable
                buffers[name] = image
                write_json(output/'report.json', report)
            report['cases'].append(dict(samplesPerFrame=spp, assessment=assess(buffers),
                fixtureSha256={name: sha(folder/name) for name in ('scene.json', 'render-preset.json')}))
            print('spp', spp, report['cases'][-1]['assessment'], flush=True)
        if any(sha(Path(p)) != digest for p, digest in artifacts.items()):
            raise ValueError('Gate probe artifacts changed')
        report['status'] = 'passed-multispp-fallback-probe'
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-multispp-fallback-probe'


if __name__ == '__main__':
    raise SystemExit(main())
