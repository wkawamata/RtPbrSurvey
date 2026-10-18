"""Assess fixed reference-selected glossy ROIs without rerendering or human verdicts."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from requalify_nrd_glossy import load_source, read_evidence, check_command
from validate_nrd_disocclusion import document_albedo
from validate_nrd_disocclusion_quality import check_reference, SEEDS
from validate_nrd_glossy_quality import (ROOT, FIXTURE, MATERIALS, RESOURCES, LIMITS,
    assess_scene, assess_depths, interior)
from validate_secondary_geometry import primary_rays
from validate_part1 import sha, write_json

ROI_LIMITS = dict(referencePairNrmse=.03, meanRgbBias=.05, nrdToNativeRmse=1.0, minimumPixels=100)
SELECTION = dict(highlightQuantile=.95, darkQuantile=.20, borderRadius=3,
    mirrorStride=2, targetRadiusFactor=.8)
EXPECTED = {name+'/'+kind for name, _, _, _ in MATERIALS for kind in ('highlight', 'border', 'dark')} | {
    name+'/mirror-proxy' for name, _, _, roughness in MATERIALS if roughness <= .15}
COLORS = dict(highlight=(70, 230, 90), border=(60, 160, 255), dark=(255, 190, 50),
    **{'mirror-proxy': (240, 70, 220)})


def validate_inventory(report):
    labels = [r.get('label') for r in report.get('records', [])]
    expected = {'reference-b'+str(depth)+'-'+str(seed) for depth in (4, 8) for seed in SEEDS}
    if (report.get('schemaVersion') != 1 or len(labels) != 4 or set(labels) != expected or
            report.get('failures') != [] or report.get('sourceEvidenceReexecuted') is not False or
            report.get('sourceRecordsRevalidated') != 12 or report.get('limits') != LIMITS or
            report.get('referenceSamplesByDepth') != {'2': 4096, '4': 8192, '8': 8192} or
            report.get('referenceSeeds') != list(SEEDS)):
        raise ValueError('Incomplete or incompatible requalified ROI source')


def load_qualified(folder):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    validate_inventory(report)
    parent = Path(report['sourceEvidence']).resolve(strict=True)
    if (report.get('sourceReportSha256') != sha(parent/'report.json') or
            report.get('scriptSha256') != sha(Path(__file__).with_name('requalify_nrd_glossy.py')) or
            report.get('runnerSha256') != sha(Path(__file__).with_name('validate_nrd_glossy_quality.py')) or
            report.get('executableSha256') != sha(ROOT/'build/Debug/RtPbrSurvey.exe')):
        raise ValueError('Requalification source provenance changed')
    expected_shaders = {'shaders_PathTracing_CSMain.cso', 'shaders_NrdConversion_CSMain.cso',
        'shaders_NrdComposition_CSMain.cso'}
    if set(report.get('shaderSha256', {})) != expected_shaders:
        raise ValueError('Incomplete fresh-reference shader provenance')
    for name, digest in report['shaderSha256'].items():
        if sha(ROOT/'build/shaders'/name) != digest:
            raise ValueError('Fresh-reference shader changed')
    scene, preset, buffers, target, two, original_records, control = load_source(parent)
    expected_reuse = [dict(sourceEvidence=str(parent), record=original_records[label]) for label in
        (*RESOURCES, 'zero-environment', 'reference-b2-11', 'reference-b2-29')]
    if report.get('reusedRecords') != expected_reuse or report.get('environmentControl') != control:
        raise ValueError('Historical reuse/control identity changed')
    for name in ('scene.json', 'render-preset.json'):
        if sha(folder/name) != sha(parent/name):
            raise ValueError('Requalification copied fixture changed')
    records = {r['label']: r for r in report['records']}
    references = {2: two}
    for depth in (4, 8):
        references[depth] = []
        for seed in SEEDS:
            label = 'reference-b'+str(depth)+'-'+str(seed)
            record = records[label]
            command = record.get('command', [])
            if not command or command[-1] != '-DisableVSync':
                raise ValueError('Missing recorded fresh-reference VSync override')
            scene_path, preset_path = folder/(label+'-scene.json'), folder/(label+'-preset.json')
            check_command(dict(record, command=command[:-1]), folder, label, 'SceneColor',
                scene_path, preset_path, 8192)
            expected_scene, expected_preset = copy.deepcopy(scene), copy.deepcopy(preset)
            expected_scene['renderPreset'] = label+'-preset.json'
            expected_preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False,
                randomSeed=seed, maxBounces=depth)
            for path, value, key in ((scene_path, expected_scene, 'sceneSha256'),
                    (preset_path, expected_preset, 'presetSha256')):
                if sha(path) != record.get(key) or json.loads(path.read_text(encoding='utf-8')) != value:
                    raise ValueError('Fresh reference settings changed: '+label)
            meta, image, diagnostics = read_evidence(folder, label, record)
            if record.get('metadata') != meta:
                raise ValueError('Fresh reference metadata changed')
            check_reference(meta, diagnostics, target, seed, environment_mode=7, sample_count=8192, max_bounces=depth)
            references[depth].append(image)
    verdict, rows = assess_scene(buffers, references[4])
    comparison = assess_depths(buffers, references)
    if (verdict != 'passed-static-glossy-fixture' or comparison['status'] != 'passed-depth-agreement' or
            report.get('status') != verdict or report.get('qualityStatus') != verdict or
            report.get('materials') != rows or report.get('bounceComparison') != comparison):
        raise ValueError('Source whole-material quality/depth aggregate is invalid or unqualified')
    return scene, buffers, target, references[4], report.get('provenanceLimitation', '')


def dilate(mask, radius):
    result = mask.copy()
    for _ in range(radius):
        padded = np.pad(result, 1)
        result = (padded[1:-1, 1:-1] | padded[:-2, 1:-1] | padded[2:, 1:-1] |
            padded[1:-1, :-2] | padded[1:-1, 2:])
    return result


def sphere_distance(origins, directions, center, radius, minimum, maximum):
    offset = origins-center
    b = np.sum(offset*directions, axis=-1)
    discriminant = b*b-np.sum(offset*offset, axis=-1)+radius*radius
    root = np.sqrt(np.maximum(discriminant, 0))
    near, far = -b-root, -b+root
    distance = np.where(near >= minimum, near, far)
    return np.where((discriminant >= 0) & (distance >= minimum) & (distance <= maximum), distance, np.inf)


def mirror_proxies(meta, scene, masks):
    if scene.get('assets') or len(scene.get('nodes', [])) != len(MATERIALS):
        raise ValueError('Mirror proxy requires the explicit four-sphere fixture')
    spheres = []
    for node in scene['nodes']:
        primitive = node.get('primitive', {})
        scale = np.asarray(node['scale'], dtype=float)
        center = np.asarray(node['translation'], dtype=float)
        radius = float(primitive.get('radius', 0))*scale[0]
        if (node.get('parentId') is not None or node.get('type') != 'primitive' or
                primitive.get('kind') != 'sphere' or node.get('visible') is not True or
                scale.shape != (3,) or not np.isfinite(scale).all() or not np.allclose(scale, scale[0]) or
                center.shape != (3,) or not np.isfinite(center).all() or not np.isfinite(radius) or radius <= 0):
            raise ValueError('Unsupported mirror-proxy geometry')
        spheres.append((node['materialId'], center, radius))
    if {s[0] for s in spheres} != {m[0] for m in MATERIALS}:
        raise ValueError('Missing or duplicate proxy sphere material')
    height, width = masks[MATERIALS[0][0]].shape
    ys, xs = np.mgrid[0:height:SELECTION['mirrorStride'], 0:width:SELECTION['mirrorStride']]
    xs, ys = xs.ravel(), ys.ravel()
    origins, directions = primary_rays(meta, xs, ys)
    distances = np.stack([sphere_distance(origins, directions, center, radius,
        meta['rayTMin'], meta['rayTMax']) for _, center, radius in spheres], axis=-1)
    owner = distances.argmin(axis=-1)
    nearest = distances.min(axis=-1)
    hit = np.isfinite(nearest)
    points = origins+directions*np.where(hit, nearest, 0)[:, None]
    centers = np.asarray([s[1] for s in spheres])[owner]
    normals = points-centers
    length = np.linalg.norm(normals, axis=-1, keepdims=True)
    normals = np.divide(normals, length, out=np.zeros_like(normals), where=length != 0)
    reflected = directions-2*np.sum(normals*directions, axis=-1, keepdims=True)*normals
    reflected_origin = points+normals*meta['normalBias']
    secondary = np.stack([sphere_distance(reflected_origin, reflected, center,
        radius*SELECTION['targetRadiusFactor'], meta['rayTMin'], meta['rayTMax'])
        for _, center, radius in spheres], axis=-1)
    secondary[np.arange(len(owner)), owner] = np.inf
    intersects_other = np.isfinite(secondary.min(axis=-1)) & hit
    results = {}
    for name, _, _, roughness in MATERIALS:
        if roughness > .15:
            continue
        index = next(i for i, sphere in enumerate(spheres) if sphere[0] == name)
        selected = intersects_other & (owner == index) & masks[name][ys, xs]
        mask = np.zeros((height, width), dtype=bool)
        mask[ys[selected], xs[selected]] = True
        results[name+'/mirror-proxy'] = mask
    return results


def select_rois(meta, scene, albedo, reference):
    if (albedo.ndim != 3 or albedo.shape[-1] != 4 or reference.shape != albedo.shape or
            not np.isfinite(albedo).all() or not np.isfinite(reference).all()):
        raise ValueError('Malformed ROI selection images')
    luminance = reference[..., :3] @ np.array([.2126, .7152, .0722])
    masks, regions = {}, {}
    for name, color, _, _ in MATERIALS:
        mask = interior((albedo[..., 3] == 1) &
            np.isclose(albedo, document_albedo(color), atol=.001, rtol=0).all(axis=-1))
        if int(mask.sum()) < ROI_LIMITS['minimumPixels']:
            raise ValueError('Missing ROI material: '+name)
        low, high = np.quantile(luminance[mask], [SELECTION['darkQuantile'], SELECTION['highlightQuantile']])
        if not 0 <= low < high:
            raise ValueError('ROI reference lacks brightness variation')
        highlight = mask & (luminance >= high)
        regions[name+'/highlight'] = highlight
        regions[name+'/dark'] = mask & (luminance <= low)
        regions[name+'/border'] = mask & ~highlight & dilate(highlight, SELECTION['borderRadius'])
        masks[name] = mask
    regions.update(mirror_proxies(meta, scene, masks))
    if set(regions) != EXPECTED:
        raise ValueError('Incomplete ROI selection inventory')
    return regions, masks


def assess_roi(nrd, native, first, second):
    arrays = [np.asarray(value, dtype=np.float64) for value in (nrd, native, first, second)]
    if (arrays[0].ndim != 2 or arrays[0].shape[1] != 3 or any(a.shape != arrays[0].shape or
            not np.isfinite(a).all() or (a < 0).any() for a in arrays)):
        raise ValueError('Malformed ROI sample cohort')
    count = len(arrays[0])
    if count < ROI_LIMITS['minimumPixels']:
        return dict(status='inconclusive-coverage', passed=False, pixels=count)
    nrd, native, first, second = arrays
    reference = (first+second)*.5
    norm = float(np.sqrt(np.mean(reference**2)))
    mean_norm = float(np.linalg.norm(reference.mean(axis=0)))
    if min(norm, mean_norm) <= 1e-6:
        return dict(status='inconclusive-dark-reference', passed=False, pixels=count,
            referenceRms=norm, referenceMeanNorm=mean_norm)
    error = lambda value: float(np.sqrt(np.mean(value**2))/norm)
    pair, nrd_error, native_error = error(first-second), error(nrd-reference), error(native-reference)
    bias = float(np.linalg.norm((nrd-reference).mean(axis=0))/mean_norm)
    qualified = pair <= ROI_LIMITS['referencePairNrmse']
    checks = dict(noRmseRegression=nrd_error <= native_error*ROI_LIMITS['nrdToNativeRmse'],
        meanRgbBias=bias <= ROI_LIMITS['meanRgbBias'])
    status = 'inconclusive-reference' if not qualified else 'passed-roi' if all(checks.values()) else 'failed-roi-quality'
    return dict(status=status, passed=status == 'passed-roi', qualified=qualified, checks=checks, pixels=count,
        referencePairNrmse=pair, nrdNrmse=nrd_error, nativeNrmse=native_error, meanRgbBias=bias,
        referenceMeanRgb=reference.mean(axis=0).tolist(), nrdMeanRgb=nrd.mean(axis=0).tolist())


def roi_verdict(rows):
    labels = [r.get('roi') for r in rows]
    if len(labels) != len(EXPECTED) or set(labels) != EXPECTED:
        raise ValueError('Incomplete, duplicate or unexpected ROI results')
    for row in rows:
        pixels, status = row.get('pixels'), row.get('status')
        if not isinstance(pixels, int) or isinstance(pixels, bool) or pixels < 0:
            raise ValueError('Invalid ROI pixel count')
        if status == 'inconclusive-coverage':
            if pixels >= ROI_LIMITS['minimumPixels'] or row.get('passed') is not False:
                raise ValueError('Inconsistent ROI coverage disposition')
            continue
        if pixels < ROI_LIMITS['minimumPixels']:
            raise ValueError('Insufficient ROI quality coverage')
        if status == 'inconclusive-dark-reference':
            values = [row.get(key) for key in ('referenceRms', 'referenceMeanNorm')]
            if (any(not isinstance(v, (float, int)) or isinstance(v, bool) or not np.isfinite(v) or v < 0 for v in values) or
                    min(values) > 1e-6 or row.get('passed') is not False):
                raise ValueError('Invalid dark-reference disposition')
            continue
        values = [row.get(key) for key in ('referencePairNrmse', 'nrdNrmse', 'nativeNrmse', 'meanRgbBias')]
        if any(not isinstance(v, (float, int)) or isinstance(v, bool) or not np.isfinite(v) or v < 0 for v in values):
            raise ValueError('Missing or invalid ROI measurements')
        pair, nrd_error, native_error, bias = values
        qualified = pair <= ROI_LIMITS['referencePairNrmse']
        checks = dict(noRmseRegression=nrd_error <= native_error*ROI_LIMITS['nrdToNativeRmse'],
            meanRgbBias=bias <= ROI_LIMITS['meanRgbBias'])
        expected_status = 'inconclusive-reference' if not qualified else 'passed-roi' if all(checks.values()) else 'failed-roi-quality'
        if (row.get('qualified') is not qualified or row.get('checks') != checks or status != expected_status or
                row.get('passed') is not (expected_status == 'passed-roi')):
            raise ValueError('ROI flags do not match numeric criteria')
    if any(row['status'] == 'failed-roi-quality' for row in rows):
        return 'failed-roi-quality'
    return 'passed-static-rois' if all(row['status'] == 'passed-roi' for row in rows) else 'inconclusive-rois'


def display_rgb(image):
    value = np.maximum(image[..., :3], 0)*8
    value = value/(1+value)
    value = np.where(value <= .0031308, value*12.92, 1.055*value**(1/2.4)-.055)
    return np.clip(value*255+.5, 0, 255).astype(np.uint8)


def preview(output, buffers, references, regions, materials):
    reference = (references[0].astype(float)+references[1])*.5
    rgb = display_rgb(reference)
    overlay = rgb.copy()
    for kind in COLORS:
        mask = np.logical_or.reduce([value for name, value in regions.items() if name.endswith('/'+kind)])
        overlay[mask] = (.5*overlay[mask]+.5*np.asarray(COLORS[kind])).astype(np.uint8)
    sheet = Image.new('RGB', (1120, 1210), (25, 25, 25))
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 6), 'Diagnostic exposure 8; Reinhard + sRGB. NOT the numeric evaluation image.', fill='white')
    draw.text((8, 24), 'Green: highlight; blue: border; yellow: dark; magenta: ideal-mirror proxy.', fill='white')
    for column, label in enumerate(('Native 30 spp', 'NRD 30 frames', 'Reference 2 x 8192 spp', 'Reference + ROI masks')):
        draw.text((column*280+8, 46), label, fill='white')
    images = [display_rgb(buffers['SceneColor']), display_rgb(buffers['NrdSceneColor']), rgb, overlay]
    for row, (name, _, _, _) in enumerate(MATERIALS):
        ys, xs = np.where(materials[name])
        box = (max(0, xs.min()-7), max(0, ys.min()-7), min(rgb.shape[1], xs.max()+8), min(rgb.shape[0], ys.max()+8))
        draw.text((8, 66+row*285), name, fill='white')
        for column, data in enumerate(images):
            crop = Image.fromarray(data).crop(box)
            crop.thumbnail((270, 260), Image.Resampling.NEAREST)
            sheet.paste(crop, (column*280+(280-crop.width)//2, 84+row*285))
    sheet.save(output/'roi-preview.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if (output == source or source in output.parents or output == FIXTURE or FIXTURE in output.parents or
            (output.exists() and any(output.iterdir()))):
        parser.error('Output must be a separate new/empty directory')
    report = dict(schemaVersion=1, status='running', sourceEvidence=str(source),
        sourceReportSha256=sha(source/'report.json'), sourceEvidenceReexecuted=False,
        freshGpuCaptures=0, fullIntegrationReady=False, subjectiveVerdict='unanswered',
        scriptSha256=sha(Path(__file__)), selection=SELECTION, limits=ROI_LIMITS, rois=[], failures=[])
    write_json(output/'report.json', report)
    try:
        scene, buffers, target, references, limitation = load_qualified(source)
        report['sourceRecordsRevalidated'] = 16
        report['historicalProvenanceLimitation'] = limitation
        regions, materials = select_rois(target, scene, buffers['Albedo'], (references[0]+references[1])*.5)
        for name in sorted(regions):
            mask = regions[name]
            row = assess_roi(*(a[mask, :3] for a in (buffers['NrdSceneColor'], buffers['SceneColor'], *references)))
            filename = name.replace('/', '__')+'.png'
            Image.fromarray(mask.astype(np.uint8)*255).save(output/filename)
            row.update(roi=name, maskFile=filename, maskSha256=sha(output/filename),
                logicalMaskSha256=hashlib.sha256(mask.astype(np.uint8).tobytes()).hexdigest())
            report['rois'].append(row)
        preview(output, buffers, references, regions, materials)
        report.update(status=roi_verdict(report['rois']), previewSha256=sha(output/'roi-preview.png'))
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = ('Fixed static sphere ROIs only. Mirror proxies use analytic ideal reflections, 80% target radius, stride 2; '
        'not actual GGX ray or triangle-hit labels. All captures reused, not rerendered. No Cornell or human acceptance.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-static-rois'


if __name__ == '__main__':
    raise SystemExit(main())
