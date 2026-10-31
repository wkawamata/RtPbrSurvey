"""Revalidate fixed glossy ROI evidence and acquire more-converged Native references."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw

from requalify_nrd_glossy import read_evidence
from validate_nrd_disocclusion_quality import check_reference, SEEDS
from validate_nrd_glossy_rois import (ROOT, FIXTURE, EXPECTED, MATERIALS, ROI_LIMITS,
    SELECTION, assess_roi, display_rgb, load_qualified, roi_verdict, select_rois)
from validate_part1 import sha, write_json

SAMPLES = 32768
TIMEOUT = 3000


def evaluate(buffers, references, regions):
    return [dict(assess_roi(*(image[regions[name], :3] for image in
        (buffers['NrdSceneColor'], buffers['SceneColor'], *references))), roi=name)
        for name in sorted(regions)]


def verify_masks(folder, report, buffers, references, regions):
    if (report.get('schemaVersion') != 1 or report.get('failures') != [] or
            report.get('selection') != SELECTION or report.get('limits') != ROI_LIMITS or
            report.get('sourceRecordsRevalidated') != 16 or
            report.get('sourceEvidenceReexecuted') is not False or report.get('freshGpuCaptures') != 0 or
            report.get('subjectiveVerdict') != 'unanswered' or report.get('fullIntegrationReady') is not False):
        raise ValueError('Incomplete or incompatible fixed ROI source')
    roi_verdict(report.get('rois', []))
    rows = evaluate(buffers, references, regions)
    saved = {row['roi']: row for row in report['rois']}
    if set(regions) != EXPECTED or report.get('status') != roi_verdict(rows):
        raise ValueError('ROI source status does not match recomputed measurements')
    for row in rows:
        name, mask = row['roi'], regions[row['roi']]
        filename = name.replace('/', '__')+'.png'
        digest = hashlib.sha256(mask.astype(np.uint8).tobytes()).hexdigest()
        expected = dict(row, maskFile=filename, maskSha256=sha(folder/filename), logicalMaskSha256=digest)
        with Image.open(folder/filename) as image:
            actual = np.asarray(image)
        if saved[name] != expected or not np.array_equal(actual, mask.astype(np.uint8)*255):
            raise ValueError('ROI source mask or numeric measurements changed: '+name)


def load_fixed(folder):
    report = json.loads((folder/'report.json').read_text(encoding='utf-8'))
    parent = Path(report['sourceEvidence']).resolve(strict=True)
    if (report.get('sourceReportSha256') != sha(parent/'report.json') or
            report.get('scriptSha256') != sha(Path(__file__).with_name('validate_nrd_glossy_rois.py')) or
            report.get('previewSha256') != sha(folder/'roi-preview.png')):
        raise ValueError('Fixed ROI source provenance changed')
    scene, buffers, target, references, limitation = load_qualified(parent)
    regions, materials = select_rois(target, scene, buffers['Albedo'], (references[0]+references[1])*.5)
    verify_masks(folder, report, buffers, references, regions)
    preset = json.loads((parent/'render-preset.json').read_text(encoding='utf-8'))
    return scene, preset, buffers, target, regions, materials, limitation


def capture_reference(output, scene, preset, target, seed):
    label = 'reference-b4-'+str(seed)
    scene, preset = copy.deepcopy(scene), copy.deepcopy(preset)
    scene['renderPreset'] = label+'-preset.json'
    preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False,
        randomSeed=seed, maxBounces=4)
    scene_path, preset_path = output/(label+'-scene.json'), output/(label+'-preset.json')
    write_json(scene_path, scene)
    write_json(preset_path, preset)
    path, log = output/(label+'.ptbuf'), output/(label+'.log')
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene_path),
        '-RenderPreset', str(preset_path), '-EnablePathTracing', '-PathTracingSamples', str(SAMPLES),
        '-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path),
        '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=TIMEOUT)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0:
        raise ValueError('Reference process failed: '+label)
    record = dict(label=label, command=command, sha256=sha(path), logSha256=sha(log), d3d12Errors=0,
        sceneSha256=sha(scene_path), presetSha256=sha(preset_path))
    meta, image, diagnostics = read_evidence(output, label, record)
    check_reference(meta, diagnostics, target, seed, environment_mode=7, sample_count=SAMPLES, max_bounces=4)
    record.update(metadata=meta, diagnostics=diagnostics)
    return image, record


def identities():
    directory = Path(__file__).parent
    files = [Path(__file__), directory/'validate_nrd_glossy_rois.py', directory/'requalify_nrd_glossy.py',
        directory/'validate_nrd_glossy_quality.py', directory/'validate_secondary_geometry.py',
        directory/'validate_nrd_disocclusion.py', directory/'validate_nrd_disocclusion_quality.py',
        directory/'validate_nrd_material_matrix.py', directory/'validate_denoiser_distances.py',
        directory/'validate_inputs.py', directory/'validate_part1.py', ROOT/'build/Debug/RtPbrSurvey.exe']
    files += [ROOT/'build/shaders'/name for name in ('shaders_PathTracing_CSMain.cso',
        'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso')]
    return {str(path.relative_to(ROOT)): sha(path) for path in files}


def preview(output, buffers, references, materials):
    images = [display_rgb(buffers['SceneColor']), display_rgb(buffers['NrdSceneColor']),
        display_rgb((references[0].astype(float)+references[1])*.5)]
    sheet = Image.new('RGB', (840, 1200), (25, 25, 25))
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 6), 'Exposure 8; Reinhard + sRGB, display only. Fixed ROIs; no colored overlays.', fill='white')
    for column, label in enumerate(('Native 30 spp (reused)', 'NRD 30 frames (reused)', 'Reference 2 x 32768 spp (fresh)')):
        draw.text((column*280+8, 28), label, fill='white')
    for row, (name, _, _, _) in enumerate(MATERIALS):
        ys, xs = np.where(materials[name])
        height, width = materials[name].shape
        box = (max(0, xs.min()-7), max(0, ys.min()-7), min(width, xs.max()+8), min(height, ys.max()+8))
        draw.text((8, 50+row*285), name, fill='white')
        for column, data in enumerate(images):
            crop = Image.fromarray(data).crop(box)
            crop.thumbnail((270, 260), Image.Resampling.NEAREST)
            sheet.paste(crop, (column*280+(280-crop.width)//2, 68+row*285))
    sheet.save(output/'comparison.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if (output == source or source in output.parents or output == FIXTURE or FIXTURE in output.parents or
            (output.exists() and (not output.is_dir() or any(output.iterdir())))):
        parser.error('Output must be a separate new/empty directory')
    report = dict(schemaVersion=1, status='running', sourceEvidence=str(source),
        sourceReportSha256=sha(source/'report.json'), sourceEvidenceReexecuted=False,
        sourceRecordsRevalidated=0, referenceSamples=SAMPLES, referenceSeeds=list(SEEDS),
        maxBounces=4, limits=ROI_LIMITS, selection=SELECTION, masksReselected=False,
        candidateGpuCaptures=0, freshGpuCaptures=0, fullIntegrationReady=False,
        subjectiveVerdict='unanswered', records=[], rois=[], failures=[], artifactSha256=identities())
    write_json(output/'report.json', report)
    try:
        scene, preset, buffers, target, regions, materials, limitation = load_fixed(source)
        report.update(sourceRecordsRevalidated=16, historicalProvenanceLimitation=limitation,
            fixedMasks={row['roi']: dict(file=row['maskFile'], sha256=row['maskSha256'],
                logicalSha256=row['logicalMaskSha256']) for row in
                json.loads((source/'report.json').read_text(encoding='utf-8'))['rois']})
        write_json(output/'report.json', report)
        print('revalidated 16 source captures and 14 fixed ROI masks', flush=True)
        references = []
        for seed in SEEDS:
            print('starting 32768 spp depth 4 seed '+str(seed), flush=True)
            image, record = capture_reference(output, scene, preset, target, seed)
            references.append(image)
            report['records'].append(record)
            report['freshGpuCaptures'] = len(references)
            write_json(output/'report.json', report)
            print('captured 32768 spp depth 4 seed '+str(seed), flush=True)
        if identities() != report['artifactSha256'] or sha(source/'report.json') != report['sourceReportSha256']:
            raise ValueError('Acquisition artifacts or source report changed during execution')
        report['rois'] = evaluate(buffers, references, regions)
        report['status'] = roi_verdict(report['rois'])
        preview(output, buffers, references, materials)
        report['previewSha256'] = sha(output/'comparison.png')
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = ('Fixed masks selected from earlier 8192-spp references; not reselected from fresh '
        'references or NRD error. Historical Native/NRD candidates reused. Two fresh depth-4 Native references; '
        'no new depth-8, Cornell, temporal or human acceptance. Mirror proxies are not actual GGX hit labels.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-static-rois'


if __name__ == '__main__':
    raise SystemExit(main())
