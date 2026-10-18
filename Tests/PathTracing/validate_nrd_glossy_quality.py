"""Qualify static high-contrast sphere reflections against independent Native samples."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess

import numpy as np

from validate_denoiser_distances import capture
from validate_inputs import read_buffer
from validate_nrd_disocclusion import document_albedo
from validate_nrd_disocclusion_quality import check_reference, SAMPLES, SEEDS
from validate_nrd_material_matrix import MATERIALS, check_metadata
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'Assets/Scene/PathTracingValidation/16-nrd-glossy-reflections'
RESOURCES = ('Albedo', 'NrdSceneColor', 'SceneColor', 'NrdInputAdmission', 'NrdCompositionDiagnostics')
LIMITS = dict(referencePairNrmse=.03, meanRgbBias=.05, nrdToNativeRmse=1.0,
    minimumContrast=2.0, minimumComposedFraction=.9, minimumPixels=100)


def check_preset(preset):
    lighting, pt = preset.get('lighting', {}), preset.get('pathTracing', {})
    if (lighting.get('skyboxEnabled') is not True or pt.get('environmentSamplingMode') != 7 or
            pt.get('environmentEnabled') is not True or pt.get('directLightingEnabled') is not False or
            pt.get('emissiveEnabled') is not False):
        raise ValueError('Glossy fixture requires environment-map importance MIS, Skybox ON and no other emitters')


def interior(mask, radius=3):
    result = mask.copy()
    for _ in range(radius):
        center = result[1:-1, 1:-1].copy()
        center &= result[:-2, 1:-1] & result[2:, 1:-1] & result[1:-1, :-2] & result[1:-1, 2:]
        result[:] = False
        result[1:-1, 1:-1] = center
    return result


def assess_reflection(nrd, native, first, second):
    arrays = [np.asarray(a, dtype=np.float64) for a in (nrd, native, first, second)]
    if (arrays[0].ndim != 2 or arrays[0].shape[1] != 3 or len(arrays[0]) < LIMITS['minimumPixels'] or
            any(a.shape != arrays[0].shape or not np.isfinite(a).all() or (a < 0).any() for a in arrays)):
        raise ValueError('Invalid glossy quality region')
    nrd, native, first, second = arrays
    reference = (first+second)*.5
    norm = float(np.sqrt(np.mean(reference**2)))
    if norm <= 1e-6:
        raise ValueError('Dark reference cannot qualify reflection quality')
    rmse = lambda a: float(np.sqrt(np.mean(a*a)))
    pair = rmse(first-second)/norm
    nrd_error, native_error = rmse(nrd-reference), rmse(native-reference)
    mean_norm = float(np.linalg.norm(reference.mean(axis=0)))
    bias = float(np.linalg.norm((nrd-reference).mean(axis=0))/mean_norm)
    luminance = reference @ np.array([.2126, .7152, .0722])
    low, high = np.percentile(luminance, [5, 95])
    contrast = float(high/max(float(low), 1e-6))
    qualification = dict(referenceConverged=pair <= LIMITS['referencePairNrmse'],
        reflectedContrast=contrast >= LIMITS['minimumContrast'])
    checks = dict(noRmseRegression=nrd_error <= native_error*LIMITS['nrdToNativeRmse'],
        meanRgbBias=bias <= LIMITS['meanRgbBias'])
    return dict(qualified=all(qualification.values()), passed=all(qualification.values()) and all(checks.values()),
        qualification=qualification, checks=checks, pixels=len(reference), referencePairNrmse=pair,
        nrdNrmse=nrd_error/norm, nativeNrmse=native_error/norm, meanRgbBias=bias,
        referenceContrast=contrast, referenceMeanRgb=reference.mean(axis=0).tolist())


def assess_scene(buffers, references):
    if set(buffers) != set(RESOURCES) or len(references) != 2:
        raise ValueError('Incomplete glossy cohort')
    shape = buffers['Albedo'].shape
    if (len(shape) != 3 or shape[-1] != 4 or any(a.shape != shape or not np.isfinite(a).all()
            for a in (*buffers.values(), *references))):
        raise ValueError('Malformed glossy image cohort')
    hit = buffers['Albedo'][..., 3] == 1
    seen = np.zeros(hit.shape, dtype=bool)
    rows = []
    for name, color, _, _ in MATERIALS:
        mask = hit & np.isclose(buffers['Albedo'], document_albedo(color), atol=.001, rtol=0).all(axis=-1)
        if (seen & mask).any():
            raise ValueError('Ambiguous glossy material identity')
        seen |= mask
        mask = interior(mask)
        count = int(mask.sum())
        used = buffers['NrdCompositionDiagnostics'][..., 0] == 1
        admitted = buffers['NrdInputAdmission'][..., 0] == 1
        if count < LIMITS['minimumPixels'] or (used & mask & ~admitted).any():
            raise ValueError('Insufficient or invalid glossy composition coverage: '+name)
        fraction = float((mask & used).sum()/count)
        if fraction < LIMITS['minimumComposedFraction']:
            raise ValueError('Native fallback hides missing NRD coverage: '+name)
        row = assess_reflection(*(image[mask, :3] for image in
            (buffers['NrdSceneColor'], buffers['SceneColor'], *references)))
        row.update(material=name, composedFraction=fraction)
        rows.append(row)
    if not np.array_equal(seen, hit):
        raise ValueError('Unknown glossy primary material')
    if (buffers['NrdInputAdmission'][~hit, 0] != 0).any() or not np.array_equal(
            buffers['NrdSceneColor'][~hit], buffers['SceneColor'][~hit]):
        raise ValueError('Glossy background must use exact Native fallback')
    if not (~hit).any():
        raise ValueError('Missing glossy background')
    if not all(row['qualification']['referenceConverged'] for row in rows):
        verdict = 'inconclusive-reference'
    elif not all(row['qualification']['reflectedContrast'] for row in rows):
        verdict = 'inconclusive-fixture'
    else:
        verdict = 'passed-static-glossy-fixture' if all(row['passed'] for row in rows) else 'failed-quality-criteria'
    return verdict, rows


def assess_environment_control(meta, image):
    if (meta.get('resource') != 'PathTracing.SceneColor' or meta.get('format') != 10 or
            meta.get('nrdPreviewRequested') is not False or meta.get('nrdPresentation') != 'Native PT' or
            meta.get('primarySampling') != 'pixel-center' or meta.get('samplesPerFrame') != 1 or
            meta.get('sampleStartIndex') != 30 or image.ndim != 3 or image.shape[-1] != 4 or
            image.size == 0 or not np.isfinite(image).all() or (image < 0).any() or
            (image[..., 3] != 1).any()):
        raise ValueError('Invalid zero-environment control capture')
    maximum = float(image[..., :3].max())
    return dict(passed=maximum <= 1e-6, maximumRgb=maximum, maximumAllowedRgb=1e-6,
        expected='zero primary and background radiance: no lights, emission or environment radiance')


def environment_capture(output, scene, preset):
    check_preset(preset)
    folder = output/'environment-control'
    scene, preset = copy.deepcopy(scene), copy.deepcopy(preset)
    for name in ('skyColor', 'groundColor', 'lightColor'):
        scene['environment'][name] = [0, 0, 0]
    for name in ('backgroundIntensity', 'lightIntensity', 'fillIntensity', 'colorPanelIntensity'):
        scene['environment'][name] = 0
    preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False)
    write_json(folder/'scene.json', scene)
    write_json(folder/'render-preset.json', preset)
    (meta, image), record = capture(ROOT/'build/Debug/RtPbrSurvey.exe', folder,
        'SceneColor', folder/'render-preset.json', 'zero-environment')
    record.update(logSha256=sha(folder/'zero-environment.log'),
        sceneSha256=sha(folder/'scene.json'), presetSha256=sha(folder/'render-preset.json'))
    return assess_environment_control(meta, image), record


def reference_capture(output, scene, preset, target, seed):
    label = 'reference-'+str(seed)
    scene, preset = copy.deepcopy(scene), copy.deepcopy(preset)
    scene['renderPreset'] = label+'-preset.json'
    preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False, randomSeed=seed)
    scene_path, preset_path = output/(label+'-scene.json'), output/(label+'-preset.json')
    write_json(scene_path, scene)
    write_json(preset_path, preset)
    path, log = output/(label+'.ptbuf'), output/(label+'.log')
    exe = ROOT/'build/Debug/RtPbrSurvey.exe'
    command = [str(exe), '-SceneFile', str(scene_path), '-RenderPreset', str(preset_path),
        '-EnablePathTracing', '-PathTracingSamples', str(SAMPLES), '-DebugPreviewResource',
        'PathTracing.SceneColor', '-CapturePath', str(path), '-LogToFile', str(log), '-ExitAfterCapture']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=300)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    text = log.read_text(encoding='utf-8-sig')
    if code != 0 or any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
        raise ValueError('Glossy reference runtime failure')
    diagnostics = [json.loads(line[len('[PathTracing] '):]) for line in text.splitlines()
        if line.startswith('[PathTracing] ')]
    if len(diagnostics) != 1:
        raise ValueError('Missing/duplicate reference diagnostics')
    meta, image = read_buffer(path)
    check_reference(meta, diagnostics[0], target, seed, environment_mode=7)
    return image.astype(np.float32), dict(label=label, command=command, sha256=sha(path),
        logSha256=sha(log), sceneSha256=sha(scene_path), presetSha256=sha(preset_path),
        metadata=meta, diagnostics=diagnostics[0], d3d12Errors=0)


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
    check_preset(json.loads((output/'render-preset.json').read_text(encoding='utf-8')))
    report = dict(schemaVersion=1, status='running', fullIntegrationReady=False, records=[], failures=[],
        executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'), scriptSha256=sha(Path(__file__)),
        fixtureSha256={name: sha(output/name) for name in ('scene.json', 'render-preset.json')},
        helperSha256={name: sha(Path(__file__).parent/name) for name in
            ('validate_nrd_material_matrix.py', 'validate_nrd_disocclusion_quality.py',
             'validate_denoiser_distances.py', 'validate_inputs.py')}, limits=LIMITS,
        referenceSamples=SAMPLES, referenceSeeds=SEEDS)
    write_json(output/'report.json', report)
    try:
        common, buffers = None, {}
        for name in RESOURCES:
            (meta, image), record = capture(ROOT/'build/Debug/RtPbrSurvey.exe', output, name,
                output/'render-preset.json', name)
            common = check_metadata(meta, name, common)
            buffers[name] = image.astype(np.float32)
            record['logSha256'] = sha(output/(name+'.log'))
            report['records'].append(record)
            write_json(output/'report.json', report)
            print('captured '+name, flush=True)
        scene = json.loads((output/'scene.json').read_text(encoding='utf-8'))
        preset = json.loads((output/'render-preset.json').read_text(encoding='utf-8'))
        control, record = environment_capture(output, scene, preset)
        report['records'].append(record)
        report['environmentControl'] = control
        write_json(output/'report.json', report)
        if not control['passed']:
            report.update(status='inconclusive-environment',
                limitation='Zero-environment control failed; intended illumination is unqualified, so no NRD quality verdict is issued.')
            write_json(output/'report.json', report)
            print(report['status'], control, flush=True)
            return 1
        references = []
        for seed in SEEDS:
            image, record = reference_capture(output, scene, preset, meta, seed)
            references.append(image)
            report['records'].append(record)
            write_json(output/'report.json', report)
            print('captured reference '+str(seed), flush=True)
        verdict, rows = assess_scene(buffers, references)
        report.update(status=verdict, materials=rows, metadata=common)
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = 'Static opaque sphere fixture, finite Native references; no temporal/motion, perfect mirror or subjective acceptance.'
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-static-glossy-fixture'


if __name__ == '__main__':
    raise SystemExit(main())
