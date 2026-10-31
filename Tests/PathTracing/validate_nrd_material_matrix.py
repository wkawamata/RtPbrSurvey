"""Validate material/lobe coverage and composition, not specular image quality."""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np

from validate_denoiser_distances import capture
from validate_nrd_composition import NAMES, assess_composition
from validate_nrd_disocclusion import document_albedo
from validate_nrd_presentation import RELAX
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'Assets/Scene/PathTracingValidation/15-nrd-material-matrix'
RESOURCES = NAMES + ('Albedo', 'MaterialF0Metallic', 'NormalRoughness')
MATERIALS = (
    ('dielectric-rough', (.65, .2, .15, 1), 0, .6),
    ('dielectric-smooth', (.2, .4, .6, 1), 0, .1),
    ('metal-rough', (.6, .5, .2, 1), 1, .6),
    ('metal-smooth', (.15, .6, .3, 1), 1, .1),
)


def check_metadata(meta, name, common):
    expected_format = 10 if name in ('Albedo', 'SceneColor', 'MaterialF0Metallic', 'NormalRoughness') else 2
    if name not in RESOURCES or meta.get('resource') != 'PathTracing.'+name or meta.get('format') != expected_format:
        raise ValueError('Wrong material capture identity/format: '+name)
    if (meta.get('nrdPresentation') != RELAX or meta.get('nrdFailure') != '' or
            meta.get('nrdLastEvaluation', {}).get('recorded') is not True):
        raise ValueError('NRD was not successfully recorded and presented')
    comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    if common is not None and comparable != common:
        raise ValueError('Mismatched material capture cohort')
    return comparable


def assess_materials(buffers, minimum=100):
    if set(buffers) != set(RESOURCES):
        raise ValueError('Missing or unexpected material-matrix resources')
    albedo = buffers['Albedo']
    if (albedo.ndim != 3 or albedo.shape[-1] != 4 or albedo.size == 0 or
            any(a.shape != albedo.shape or not np.isfinite(a).all() for a in buffers.values())):
        raise ValueError('Malformed material-matrix cohort')
    hit = albedo[..., 3] == 1
    seen = np.zeros(hit.shape, dtype=bool)
    rows = []
    for name, color, metallic, roughness in MATERIALS:
        expected = document_albedo(color)
        mask = hit & np.isclose(albedo, expected, atol=.001, rtol=0).all(axis=-1)
        if int(mask.sum()) < minimum or (seen & mask).any():
            raise ValueError('Missing or ambiguous material coverage: '+name)
        seen |= mask
        material = buffers['MaterialF0Metallic'][mask]
        f0 = expected[:3] if metallic else np.full(3, .04)
        if (not np.allclose(material[:, 3], metallic, atol=.001, rtol=0) or
                not np.allclose(material[:, :3], f0, atol=.002, rtol=0) or
                not np.allclose(buffers['NormalRoughness'][mask, 3], roughness, atol=.001, rtol=0)):
            raise ValueError('Incorrect metallic/F0/roughness: '+name)
        admitted = mask & (buffers['NrdInputAdmission'][..., 0] == 1)
        used = mask & (buffers['NrdCompositionDiagnostics'][..., 0] == 1)
        if int(used.sum()) < minimum or (used & ~admitted).any():
            raise ValueError('Material did not exercise admitted NRD composition: '+name)
        inactive = buffers['NrdInputAdmission'][admitted, 2]
        if (inactive != metallic).any():
            raise ValueError('Wrong diffuse activity for material: '+name)
        if metallic and (buffers['PhysicalDiffuse'][mask, :3] != 0).any():
            raise ValueError('Pure metal contains physical diffuse radiance')
        rows.append(dict(material=name, pixels=int(mask.sum()), admittedPixels=int(admitted.sum()),
            composedPixels=int(used.sum()), metallic=metallic, roughness=roughness))
    if not np.array_equal(seen, hit) or int((~hit).sum()) < minimum:
        raise ValueError('Unknown surface or missing background')
    if (buffers['NrdInputAdmission'][~hit, 0] != 0).any():
        raise ValueError('Background was admitted')
    result = assess_composition(buffers)
    result['materials'] = rows
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output == FIXTURE or FIXTURE in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    output.mkdir(parents=True, exist_ok=True)
    for name in ('scene.json', 'render-preset.json'):
        shutil.copyfile(FIXTURE/name, output/name)
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    report = dict(schemaVersion=1, status='running', fullIntegrationReady=False,
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)), records=[], failures=[],
        fixtureSha256={name: sha(output/name) for name in ('scene.json', 'render-preset.json')})
    write_json(output/'report.json', report)
    try:
        buffers, common = {}, None
        for name in RESOURCES:
            (meta, image), record = capture(exe, output, name, output/'render-preset.json', name)
            report['records'].append(record)
            common = check_metadata(meta, name, common)
            buffers[name] = image
            write_json(output/'report.json', report)
            print('captured '+name, flush=True)
        report.update(status='passed-material-contract', assessment=assess_materials(buffers), metadata=common)
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'Static planar white-environment material/lobe/composition contract only; no reflection quality, temporal or subjective acceptance.'
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-material-contract'


if __name__ == '__main__':
    raise SystemExit(main())
