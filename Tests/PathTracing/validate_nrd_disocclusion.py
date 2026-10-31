"""Qualify visibility/MV/history on newly revealed surfaces; report color without a quality verdict."""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np

from validate_inputs import project
from validate_secondary_geometry import primary_rays, planes_from_scene, intersect_planes
from validate_nrd_camera_motion import capture, assess_frame, assess_motion
from validate_nrd_presentation import RELAX
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'Assets/Scene/PathTracingValidation/14-nrd-disocclusion'
RESOURCES = ('Albedo', 'MotionVectors', 'NrdSceneColor', 'SceneColor')
CASES = (('static', 0, True), ('positive', 20, True),
    ('negative', -20, True), ('reset-control', 20, False))


def document_albedo(color):
    rgb = np.asarray(color[:3], dtype=np.float64)
    srgb = np.where(rgb <= .0031308, rgb*12.92, 1.055*rgb**(1/2.4)-.055)
    encoded = np.floor(np.clip(srgb, 0, 1)*255+.5)/255
    decoded = np.where(encoded <= .04045, encoded/12.92, ((encoded+.055)/1.055)**2.4)
    return np.r_[decoded, color[3]]


def check_cohort(meta, resource, cohort):
    expected_format = 34 if resource == 'MotionVectors' else 10 if resource in ('Albedo', 'SceneColor') else 2
    if meta.get('resource') != 'PathTracing.'+resource or meta.get('format') != expected_format:
        raise ValueError('Wrong capture identity/format')
    if (meta.get('nrdPresentation') != RELAX or meta.get('nrdFailure') != '' or
            meta.get('nrdLastEvaluation', {}).get('recorded') is not True):
        raise ValueError('NRD was not successfully recorded and presented')
    comparable = {key: value for key, value in meta.items() if key not in ('resource', 'format')}
    if cohort is not None and comparable != cohort:
        raise ValueError('Mismatched disocclusion capture cohort')
    return comparable


def visibility_regions(meta, scene, stride=4):
    if meta.get('primarySampling') != 'pixel-center' or stride < 1:
        raise ValueError('Requires pixel-center primary sampling')
    ys, xs = np.mgrid[16:meta['height']-16:stride, 16:meta['width']-16:stride]
    xs, ys = xs.ravel(), ys.ravel()
    planes = planes_from_scene(scene)
    if len(planes) != 2:
        raise ValueError('Expected foreground and background planes')
    origins, directions = primary_rays(meta, xs, ys)
    distances, indices, edges = intersect_planes(origins, directions, planes, meta['rayTMin'], meta['rayTMax'])
    world = origins + directions*np.where(indices >= 0, distances, 0)[:, None]
    ndc = project(world, meta['previousViewProjection'])
    previous_x = (ndc[:, 0]+1)*meta['width']/2-.5
    previous_y = (1-ndc[:, 1])*meta['height']/2-.5
    previous = dict(meta, inverseViewProjection=np.linalg.inv(np.asarray(meta['previousViewProjection'])).tolist())
    prior_origins, prior_directions = primary_rays(previous, previous_x, previous_y)
    _, prior_indices, prior_edges = intersect_planes(prior_origins, prior_directions, planes,
        meta['rayTMin'], meta['rayTMax'])
    inside = (abs(ndc[:, :2]) < 1).all(axis=-1) & (ndc[:, 2] >= 0) & (ndc[:, 2] <= 1)
    valid = (indices >= 0) & ~edges & inside & ~prior_edges
    revealed = valid & (indices == 1) & (prior_indices == 0)
    retained = valid & (indices == 1) & (prior_indices == 1)
    return xs, ys, indices, edges, revealed, retained


def assess_visibility(meta, albedo, scene, moving):
    if meta.get('resource') != 'PathTracing.Albedo' or meta.get('format') != 10:
        raise ValueError('Expected primary Albedo identity/format')
    if albedo.shape != (meta['height'], meta['width'], 4) or not np.isfinite(albedo).all():
        raise ValueError('Malformed primary Albedo')
    xs, ys, indices, edges, revealed, retained = visibility_regions(meta, scene)
    for index in (0, 1):
        mask = (indices == index) & ~edges
        if mask.sum() < 100:
            raise ValueError('Insufficient independent material coverage')
        color = document_albedo(scene['materials'][index]['baseColor'])
        bound = 2*np.abs(np.spacing(np.abs(color).astype(np.float16)).astype(float))+3e-6
        if not (abs(albedo[ys[mask], xs[mask]]-color) <= bound).all():
            raise ValueError('Observed material does not match independent first-hit geometry')
    miss = (indices < 0) & ~edges
    if (albedo[ys[miss], xs[miss], 3] != 0).any():
        raise ValueError('Primary miss classified as a surface')
    if retained.sum() < 100 or (moving and revealed.sum() < 20) or (not moving and revealed.any()):
        raise ValueError('Missing revealed/retained coverage or static visibility changed')
    return dict(sampledRevealedPixels=int(revealed.sum()), sampledRetainedPixels=int(retained.sum()),
        samplingStride=4, foregroundPixels=int(((indices == 0) & ~edges).sum()),
        albedoReference='linear -> rounded sRGB8 -> decoded linear',
        albedoBound='2 ULP(decoded half) + 3e-6')


def measure_color(meta, scene, composed, native):
    shape = (meta['height'], meta['width'], 4)
    for image in (composed, native):
        if image.shape != shape or not np.isfinite(image).all() or (image < 0).any() or (image[..., 3] != 1).any():
            raise ValueError('Invalid scene-color image')
    xs, ys, _, _, revealed, retained = visibility_regions(meta, scene)
    result = dict(qualityVerdict='not-evaluated', reference='same-frame noisy Native, not converged ground truth')
    for name, mask in (('revealed', revealed), ('retained', retained)):
        if not mask.any():
            result[name] = None
            continue
        a, b = composed[ys[mask], xs[mask], :3], native[ys[mask], xs[mask], :3]
        result[name] = dict(sampledPixels=int(mask.sum()), nrdMeanRgb=a.mean(axis=0).tolist(),
            nativeMeanRgb=b.mean(axis=0).tolist(), meanAbsoluteDifference=float(abs(a-b).mean()))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.output.resolve()
    if folder == FIXTURE or FIXTURE in folder.parents or (folder.exists() and any(folder.iterdir())):
        parser.error('Output must be a separate empty directory')
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(FIXTURE/'scene.json', folder/'scene.json')
    shutil.copyfile(FIXTURE/'render-preset.json', folder/'preset.json')
    shutil.copyfile(FIXTURE/'render-preset.json', folder/'render-preset.json')
    scene = json.loads((folder/'scene.json').read_text(encoding='utf-8'))
    report = dict(status='running', qualityVerdict='not-evaluated', fullIntegrationReady=False,
        records=[], cases=[], failures=[], executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'),
        scriptSha256=sha(Path(__file__)), fixtureSha256={name: sha(FIXTURE/name) for name in
            ('scene.json', 'render-preset.json')},
        helperSha256={name: sha(Path(__file__).parent/name) for name in
            ('validate_nrd_camera_motion.py', 'validate_secondary_geometry.py', 'validate_inputs.py')},
        shaderSha256={name: sha(ROOT/'build/shaders'/name) for name in
            ('shaders_PathTracing_CSMain.cso', 'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso')})
    manifest = ROOT/'build/shaders/nrd-relax-manifest.txt'
    report['sdkShaderManifestSha256'] = sha(manifest)
    report['sdkShaderSha256'] = {name: sha(ROOT/f'build/shaders/nrd-relax-{index}.cso')
        for index, name in enumerate(manifest.read_text().splitlines())}
    write_json(folder/'report.json', report)
    try:
        native_cohorts = {}
        for case, angle, continuous in CASES:
            cohort, buffers, metadata = None, {}, {}
            entry = dict(case=case, angle=angle, continuous=continuous)
            report['cases'].append(entry)
            for resource in RESOURCES:
                meta, image, record = capture(folder, case+'-'+resource, resource, angle, continuous)
                report['records'].append(record)
                cohort = check_cohort(meta, resource, cohort)
                record['evaluation'] = assess_frame(meta, continuous, angle != 0)
                buffers[resource], metadata[resource] = image, meta
                write_json(folder/'report.json', report)
                print('captured '+case+' '+resource, flush=True)
            entry['visibility'] = assess_visibility(metadata['Albedo'], buffers['Albedo'], scene, angle != 0)
            entry['motion'] = assess_motion(metadata['MotionVectors'], buffers['MotionVectors'], scene, angle != 0)
            entry['colorMeasurements'] = measure_color(metadata['Albedo'], scene,
                buffers['NrdSceneColor'], buffers['SceneColor'])
            entry['metadata'] = cohort
            native_cohorts[case] = {key: value for key, value in cohort.items() if key != 'nrdLastEvaluation'}
            write_json(folder/'report.json', report)
        if native_cohorts['positive'] != native_cohorts['reset-control']:
            raise ValueError('Reset control native camera/sample state does not match continuous case')
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'Static geometry, +/-20 degree orbit; geometry/MV/history/finite-color gate only. No converged radiance reference, visual ghosting acceptance, reverse/settling or object-motion qualification. Reset control also resets SDK frame index.'
    write_json(folder/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
