"""Compare qualified motion captures to independent high-sample Native references."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer, project
from validate_secondary_geometry import primary_rays
from validate_nrd_disocclusion import (ROOT, FIXTURE, CASES, RESOURCES, check_cohort,
    visibility_regions, assess_visibility)
from validate_nrd_camera_motion import assess_frame, assess_motion
from validate_part1 import sha, write_json

SAMPLES = 1024
SEEDS = (11, 29)
LIMITS = dict(referencePairNrmse=.03, meanRgbBias=.05, meanRedExcess=.02,
    p95RedExcess=.05, nrdToNativeRmse=1.0)


def assess_region(nrd, native, first, second):
    arrays = [np.asarray(value, dtype=np.float64) for value in (nrd, native, first, second)]
    if (arrays[0].ndim != 2 or arrays[0].shape[1] != 3 or arrays[0].shape[0] < 20 or
            any(value.shape != arrays[0].shape or not np.isfinite(value).all() or
                (value < 0).any() for value in arrays)):
        raise ValueError('Malformed or insufficient quality region')
    nrd, native, first, second = arrays
    reference = (first+second)*.5
    norm = float(np.sqrt(np.mean(reference**2)))
    green = float(reference[:, 1].mean())
    if min(norm, green) < 1e-6:
        raise ValueError('Reference lacks required background radiance')
    nrmse = lambda value: float(np.sqrt(np.mean(value**2))/norm)
    pair_error = nrmse(first-second)
    nrd_error, native_error = nrmse(nrd-reference), nrmse(native-reference)
    bias = float(np.linalg.norm((nrd-reference).mean(axis=0))/np.linalg.norm(reference.mean(axis=0)))
    red = np.maximum(0, nrd[:, 0]-reference[:, 0])/green
    mean_red, p95_red = float(red.mean()), float(np.percentile(red, 95))
    checks = dict(referenceConverged=pair_error <= LIMITS['referencePairNrmse'],
        noRmseRegression=nrd_error <= native_error*LIMITS['nrdToNativeRmse'],
        meanRgbBias=bias <= LIMITS['meanRgbBias'], meanRedExcess=mean_red <= LIMITS['meanRedExcess'],
        p95RedExcess=p95_red <= LIMITS['p95RedExcess'])
    return dict(passed=all(checks.values()), checks=checks, sampledPixels=len(nrd),
        referencePairNrmse=pair_error, nrdNrmse=nrd_error, nativeNrmse=native_error,
        meanRgbBias=bias, meanRedExcess=mean_red, p95RedExcess=p95_red,
        referenceMeanRgb=reference.mean(axis=0).tolist(), nrdMeanRgb=nrd.mean(axis=0).tolist())


def validate_inventory(report):
    expected = {case+'-'+resource for case, _, _ in CASES for resource in RESOURCES}
    records = report.get('records', [])
    names = [record.get('name') for record in records]
    cases = [entry.get('case') for entry in report.get('cases', [])]
    if (report.get('status') != 'passed' or len(names) != len(expected) or set(names) != expected or
            len(cases) != len(CASES) or set(cases) != {case for case, _, _ in CASES} or
            report.get('failures') != []):
        raise ValueError('Incomplete, duplicate or failed motion source report')


def load_source(folder):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    validate_inventory(report)
    if (set(report.get('fixtureSha256', {})) != {'scene.json', 'render-preset.json'} or
            set(report.get('helperSha256', {})) != {'validate_nrd_camera_motion.py',
                'validate_secondary_geometry.py', 'validate_inputs.py'} or
            set(report.get('shaderSha256', {})) != {'shaders_PathTracing_CSMain.cso',
                'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso'}):
        raise ValueError('Incomplete motion source provenance')
    if (report['executableSha256'] != sha(ROOT/'build/Debug/RtPbrSurvey.exe') or
            report['scriptSha256'] != sha(ROOT/'Tests/PathTracing/validate_nrd_disocclusion.py')):
        raise ValueError('Motion evidence executable/runner differs from current source')
    for name, digest in report['fixtureSha256'].items():
        if sha(FIXTURE/name) != digest:
            raise ValueError('Motion fixture changed')
    for name, digest in report['helperSha256'].items():
        if sha(Path(__file__).parent/name) != digest:
            raise ValueError('Motion helper changed')
    for name, digest in report['shaderSha256'].items():
        if sha(ROOT/'build/shaders'/name) != digest:
            raise ValueError('Motion shader changed')
    manifest = ROOT/'build/shaders/nrd-relax-manifest.txt'
    if report['sdkShaderManifestSha256'] != sha(manifest):
        raise ValueError('SDK shader manifest changed')
    identifiers = manifest.read_text().splitlines()
    if set(report['sdkShaderSha256']) != set(identifiers):
        raise ValueError('Incomplete SDK shader provenance')
    for index, name in enumerate(identifiers):
        if sha(ROOT/f'build/shaders/nrd-relax-{index}.cso') != report['sdkShaderSha256'][name]:
            raise ValueError('SDK shader changed')
    scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))
    records = {record['name']: record for record in report['records']}
    outputs = {}
    for case, angle, continuous in CASES:
        common, buffers, metadata = None, {}, {}
        for resource in RESOURCES:
            name = case+'-'+resource
            record = records[name]
            path, log = folder/(name+'.ptbuf'), folder/(name+'.log')
            if record.get('resource') != resource or sha(path) != record['sha256'] or sha(log) != record['logSha256']:
                raise ValueError('Motion capture identity/hash changed: '+name)
            if any(token in log.read_text(encoding='utf-8-sig') for token in ('[ERROR]', '[CORRUPTION]')):
                raise ValueError('Motion D3D12 error: '+name)
            meta, image = read_buffer(path)
            common = check_cohort(meta, resource, common)
            assess_frame(meta, continuous, angle != 0)
            metadata[resource], buffers[resource] = meta, image.astype(np.float32)
        assess_visibility(metadata['Albedo'], buffers['Albedo'], scene, angle != 0)
        assess_motion(metadata['MotionVectors'], buffers['MotionVectors'], scene, angle != 0)
        if case != 'static':
            outputs[case] = (metadata['Albedo'], buffers['NrdSceneColor'], buffers['SceneColor'])
    return scene, outputs


def check_reference(meta, diagnostics, target, seed):
    if (meta.get('resource') != 'PathTracing.SceneColor' or meta.get('format') != 10 or
            meta.get('samplesPerFrame') != 1 or meta.get('sampleStartIndex') != SAMPLES or
            meta.get('randomSeed') != seed or meta.get('primarySampling') != 'pixel-center' or
            meta.get('nrdPreviewRequested') is not False or meta.get('nrdPresentation') != 'Native PT'):
        raise ValueError('Wrong reference capture identity/sampling state')
    expected = dict(accumulatedSamples=SAMPLES, targetSamples=SAMPLES, randomSeed=seed,
        samplesPerFrame=1, maxBounces=2, environmentEnabled=True, environmentSamplingMode=5,
        directLightingEnabled=False, emissiveEnabled=False, russianRoulette=False)
    if any(diagnostics.get(key) != value for key, value in expected.items()):
        raise ValueError('Reference runtime transport/sample target differs')
    for key in ('width', 'height', 'rayTMin', 'rayTMax', 'normalBias', 'lensShift'):
        if meta.get(key) != target[key]:
            raise ValueError('Reference geometry/dimensions differ: '+key)
    if not np.allclose(meta['viewProjection'], target['viewProjection'], atol=2e-6, rtol=2e-6):
        raise ValueError('Reference camera matrix differs')
    xs, ys = np.meshgrid(np.arange(16, meta['width']-16, 16), np.arange(16, meta['height']-16, 16))
    origins, directions = primary_rays(target, xs.ravel(), ys.ravel())
    ndc = project(origins+directions*5, meta['viewProjection'])
    pixels = (ndc[:, :2]*[1, -1]+1)*[meta['width']/2, meta['height']/2]-.5
    if (abs(pixels-np.column_stack((xs.ravel(), ys.ravel()))) > .01).any():
        raise ValueError('Reference projection exceeds 0.01 pixel alignment bound')


def capture_reference(folder, scene, target, seed, label):
    scene = copy.deepcopy(scene)
    scene['camera'].update(position=target['cameraPosition'], target=target['cameraTarget'])
    scene['renderPreset'] = label+'-preset.json'
    preset = json.loads((FIXTURE/'render-preset.json').read_text(encoding='utf-8'))
    preset['pathTracing'].update(samplesPerFrame=1, randomSeed=seed,
        denoiserDistanceDiagnostics=False, nrdPreview=False)
    scene_path, preset_path = folder/(label+'-scene.json'), folder/(label+'-preset.json')
    write_json(scene_path, scene)
    write_json(preset_path, preset)
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene_path),
        '-RenderPreset', str(preset_path), '-EnablePathTracing', '-PathTracingSamples', str(SAMPLES),
        '-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path),
        '-LogToFile', str(log), '-ExitAfterCapture']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=300)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    text = log.read_text(encoding='utf-8-sig')
    if code != 0 or any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
        raise ValueError('Reference runtime failure: '+label)
    records = [json.loads(line[len('[PathTracing] '):]) for line in text.splitlines() if line.startswith('[PathTracing] ')]
    if len(records) != 1:
        raise ValueError('Missing/duplicate reference runtime diagnostics')
    meta, image = read_buffer(path)
    check_reference(meta, records[0], target, seed)
    return image.astype(np.float32), dict(label=label, command=command, sha256=sha(path),
        logSha256=sha(log), sceneSha256=sha(scene_path), presetSha256=sha(preset_path),
        metadata=meta, diagnostics=records[0], d3d12Errors=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, folder = args.source.resolve(strict=True), args.output.resolve()
    if folder == source or source in folder.parents or folder == FIXTURE or FIXTURE in folder.parents or (
            folder.exists() and any(folder.iterdir())):
        parser.error('Output must be a separate empty directory')
    report = dict(status='running', qualityVerdict='not-evaluated', fullIntegrationReady=False,
        sourceEvidence=str(source), sourceReportSha256=sha(source/'report.json'),
        sourceEvidenceReexecuted=False, freshReferenceSamples=SAMPLES, referenceSeeds=SEEDS,
        limits=LIMITS, records=[], cases=[], failures=[], scriptSha256=sha(Path(__file__)),
        executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'))
    write_json(folder/'report.json', report)
    try:
        scene, outputs = load_source(source)
        report['sourceEvidenceRevalidated'] = True
        for direction in ('positive', 'negative'):
            meta = outputs[direction][0]
            references = []
            for seed in SEEDS:
                image, record = capture_reference(folder, scene, meta, seed, direction+'-reference-'+str(seed))
                references.append(image)
                report['records'].append(record)
                write_json(folder/'report.json', report)
                print('captured '+record['label'], flush=True)
            for case in ((direction, 'reset-control') if direction == 'positive' else (direction,)):
                target, nrd, native = outputs[case]
                xs, ys, _, _, revealed, retained = visibility_regions(target, scene)
                entry = dict(case=case, regions={})
                for name, mask, minimum in (('revealed', revealed, 20), ('retained', retained, 100)):
                    if mask.sum() < minimum:
                        raise ValueError('Insufficient quality region: '+case+' '+name)
                    values = [image[ys[mask], xs[mask], :3] for image in (nrd, native, *references)]
                    entry['regions'][name] = assess_region(*values)
                report['cases'].append(entry)
                write_json(folder/'report.json', report)
        regions = [value for entry in report['cases'] for value in entry['regions'].values()]
        reference_ok = all(value['checks']['referenceConverged'] for value in regions)
        passed = all(value['passed'] for value in regions)
        report.update(status='passed' if passed else 'failed', qualityVerdict='passed-fixed-fixture' if passed
            else 'failed-fixed-fixture' if reference_ok else 'inconclusive-reference')
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'One opaque two-plane fixture, one moving checkpoint per direction, same binary/shaders as historical motion evidence. Numeric fixture gate only; no subjective, reversal/settling, object-motion or general denoising acceptance. Reset control also changes SDK frame index.'
    write_json(folder/'report.json', report)
    print(report['status'], report['qualityVerdict'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
