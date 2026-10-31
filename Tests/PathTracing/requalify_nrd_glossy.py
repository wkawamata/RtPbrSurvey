"""Revalidate existing glossy evidence and replace only four/eight-bounce references."""
import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_nrd_disocclusion_quality import check_reference, SEEDS
from validate_nrd_glossy_quality import (ROOT, FIXTURE, RESOURCES, LIMITS, DEPTHS,
    assess_scene, assess_depths, assess_environment_control, check_candidate,
    check_metadata, check_preset)
from validate_part1 import sha, write_json


def validate_inventory(report):
    expected = set(RESOURCES) | {'zero-environment'} | {
        'reference-b'+str(depth)+'-'+str(seed) for depth in DEPTHS for seed in SEEDS}
    labels = [r.get('label') for r in report.get('records', [])]
    if (report.get('schemaVersion') != 1 or len(labels) != len(expected) or set(labels) != expected or
            report.get('failures') != [] or report.get('maxBounces') != 4 or
            report.get('referenceDepths') != list(DEPTHS) or report.get('referenceSeeds') != list(SEEDS) or
            report.get('referenceSamples') != 4096 or report.get('limits') != LIMITS):
        raise ValueError('Incomplete, duplicate or incompatible glossy source inventory')


def read_evidence(folder, label, record):
    path, log = folder/(label+'.ptbuf'), folder/(label+'.log')
    if (record.get('label') != label or record.get('d3d12Errors') != 0 or
            sha(path) != record.get('sha256') or sha(log) != record.get('logSha256')):
        raise ValueError('Source capture/log identity or hash changed: '+label)
    text = log.read_text(encoding='utf-8-sig')
    if any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
        raise ValueError('Source D3D12 error: '+label)
    diagnostics = [json.loads(line[len('[PathTracing] '):]) for line in text.splitlines()
        if line.startswith('[PathTracing] ')]
    if len(diagnostics) != 1:
        raise ValueError('Missing/duplicate source runtime record: '+label)
    if 'diagnostics' in record and diagnostics[0] != record['diagnostics']:
        raise ValueError('Source runtime record changed: '+label)
    meta, image = read_buffer(path)
    return meta, image.astype(np.float32), diagnostics[0]


def check_command(record, folder, label, resource, scene_path, preset_path, samples=None):
    expected = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene_path),
        '-RenderPreset', str(preset_path), '-EnablePathTracing']
    if samples is not None:
        expected += ['-PathTracingSamples', str(samples)]
    expected += ['-DebugPreviewResource', 'PathTracing.'+resource,
        '-CapturePath', str(folder/(label+'.ptbuf'))]
    if samples is None:
        expected += ['-CaptureAfterFrames', '30']
    expected += ['-LogToFile', str(folder/(label+'.log')), '-ExitAfterCapture']
    if record.get('command') != expected:
        raise ValueError('Source command does not match capture identity: '+label)


def load_source(source):
    report = json.loads((source/'report.json').read_text(encoding='utf-8'))
    validate_inventory(report)
    if (report.get('executableSha256') != sha(ROOT/'build/Debug/RtPbrSurvey.exe') or
            report.get('scriptSha256') != sha(Path(__file__).with_name('validate_nrd_glossy_quality.py'))):
        raise ValueError('Source executable/runner differs from current version')
    expected_helpers = {'validate_nrd_material_matrix.py', 'validate_nrd_disocclusion_quality.py',
        'validate_denoiser_distances.py', 'validate_inputs.py'}
    if set(report.get('helperSha256', {})) != expected_helpers:
        raise ValueError('Incomplete source helper provenance')
    for name, digest in report['helperSha256'].items():
        if sha(Path(__file__).with_name(name)) != digest:
            raise ValueError('Source helper changed: '+name)
    if set(report.get('fixtureSha256', {})) != {'scene.json', 'render-preset.json'}:
        raise ValueError('Incomplete source scene provenance')
    for name, digest in report['fixtureSha256'].items():
        if sha(source/name) != digest:
            raise ValueError('Copied source fixture changed: '+name)
    scene = json.loads((source/'scene.json').read_text(encoding='utf-8'))
    preset = json.loads((source/'render-preset.json').read_text(encoding='utf-8'))
    check_preset(preset)
    records = {record['label']: record for record in report['records']}
    buffers, common, target = {}, None, None
    for name in RESOURCES:
        record = records[name]
        check_command(record, source, name, name, source/'scene.json', source/'render-preset.json')
        meta, image, diagnostics = read_evidence(source, name, record)
        if record.get('resource') != name:
            raise ValueError('Wrong source candidate resource')
        common = check_metadata(meta, name, common)
        check_candidate(meta, diagnostics)
        buffers[name], target = image, meta
    control_folder = source/'environment-control'
    control_record = records['zero-environment']
    check_command(control_record, control_folder, 'zero-environment', 'SceneColor',
        control_folder/'scene.json', control_folder/'render-preset.json')
    expected_scene, expected_preset = copy.deepcopy(scene), copy.deepcopy(preset)
    for name in ('skyColor', 'groundColor', 'lightColor'):
        expected_scene['environment'][name] = [0, 0, 0]
    for name in ('backgroundIntensity', 'lightIntensity', 'fillIntensity', 'colorPanelIntensity'):
        expected_scene['environment'][name] = 0
    expected_preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False)
    for name, value, key in (('scene.json', expected_scene, 'sceneSha256'),
            ('render-preset.json', expected_preset, 'presetSha256')):
        path = control_folder/name
        if sha(path) != control_record.get(key) or json.loads(path.read_text(encoding='utf-8')) != value:
            raise ValueError('Source zero-environment settings changed')
    meta, image, diagnostics = read_evidence(control_folder, 'zero-environment', control_record)
    if (diagnostics.get('maxBounces') != 4 or diagnostics.get('environmentSamplingMode') != 7 or
            diagnostics.get('directLightingEnabled') is not False or diagnostics.get('emissiveEnabled') is not False):
        raise ValueError('Source zero-environment transport differs')
    control = assess_environment_control(meta, image)
    if not control['passed'] or control != report.get('environmentControl'):
        raise ValueError('Source environment control failed or aggregate changed')
    references = {}
    for depth in DEPTHS:
        references[depth] = []
        for seed in SEEDS:
            label = 'reference-b'+str(depth)+'-'+str(seed)
            record = records[label]
            scene_path, preset_path = source/(label+'-scene.json'), source/(label+'-preset.json')
            check_command(record, source, label, 'SceneColor', scene_path, preset_path, 4096)
            expected_scene, expected_preset = copy.deepcopy(scene), copy.deepcopy(preset)
            expected_scene['renderPreset'] = label+'-preset.json'
            expected_preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False,
                randomSeed=seed, maxBounces=depth)
            for path, value, key in ((scene_path, expected_scene, 'sceneSha256'),
                    (preset_path, expected_preset, 'presetSha256')):
                if sha(path) != record.get(key) or json.loads(path.read_text(encoding='utf-8')) != value:
                    raise ValueError('Source reference settings changed: '+label)
            meta, image, diagnostics = read_evidence(source, label, record)
            if meta != record.get('metadata'):
                raise ValueError('Source reference metadata changed: '+label)
            check_reference(meta, diagnostics, target, seed, environment_mode=7,
                sample_count=4096, max_bounces=depth)
            references[depth].append(image)
    verdict, rows = assess_scene(buffers, references[4])
    comparison = assess_depths(buffers, references)
    status = comparison['status'] if verdict == 'passed-static-glossy-fixture' and comparison[
        'status'] != 'passed-depth-agreement' else verdict
    if (report.get('status') != status or report.get('qualityStatus') != verdict or
            report.get('materials') != rows or report.get('bounceComparison') != comparison or
            report.get('metadata') != common):
        raise ValueError('Source aggregate does not match recomputed measurements')
    return scene, preset, buffers, target, references[2], records, control


def capture_reference(output, scene, preset, target, depth, seed):
    label = 'reference-b'+str(depth)+'-'+str(seed)
    scene, preset = copy.deepcopy(scene), copy.deepcopy(preset)
    scene['renderPreset'] = label+'-preset.json'
    preset['pathTracing'].update(nrdPreview=False, denoiserDistanceDiagnostics=False,
        randomSeed=seed, maxBounces=depth)
    scene_path, preset_path = output/(label+'-scene.json'), output/(label+'-preset.json')
    write_json(scene_path, scene)
    write_json(preset_path, preset)
    path, log = output/(label+'.ptbuf'), output/(label+'.log')
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(scene_path),
        '-RenderPreset', str(preset_path), '-EnablePathTracing', '-PathTracingSamples', '8192',
        '-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path),
        '-LogToFile', str(log), '-ExitAfterCapture', '-DisableVSync']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=1000)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    if code != 0:
        raise ValueError('Reference process failed: '+label)
    record = dict(label=label, command=command, sha256=sha(path), logSha256=sha(log), d3d12Errors=0,
        sceneSha256=sha(scene_path), presetSha256=sha(preset_path))
    meta, image, diagnostics = read_evidence(output, label, record)
    check_reference(meta, diagnostics, target, seed, environment_mode=7, sample_count=8192, max_bounces=depth)
    record.update(metadata=meta, diagnostics=diagnostics)
    return image, record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if (output == source or source in output.parents or output == FIXTURE or FIXTURE in output.parents or
            (output.exists() and any(output.iterdir()))):
        parser.error('Output must be a separate new/empty directory')
    report = dict(schemaVersion=1, status='running', fullIntegrationReady=False,
        sourceEvidence=str(source), sourceReportSha256=sha(source/'report.json'),
        sourceEvidenceReexecuted=False, sourceRecordsRevalidated=0, records=[], failures=[],
        referenceSamplesByDepth={'2': 4096, '4': 8192, '8': 8192}, referenceSeeds=SEEDS,
        limits=LIMITS, executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'),
        scriptSha256=sha(Path(__file__)), runnerSha256=sha(Path(__file__).with_name('validate_nrd_glossy_quality.py')),
        shaderSha256={name: sha(ROOT/'build/shaders'/name) for name in
            ('shaders_PathTracing_CSMain.cso', 'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso')},
        provenanceLimitation='Shader hashes apply to current references; historical source runner did not record shader hashes.')
    write_json(output/'report.json', report)
    try:
        scene, preset, buffers, target, two, source_records, control = load_source(source)
        report.update(sourceRecordsRevalidated=12, environmentControl=control,
            reusedRecords=[dict(sourceEvidence=str(source), record=source_records[label]) for label in
                (*RESOURCES, 'zero-environment', 'reference-b2-11', 'reference-b2-29')])
        for name in ('scene.json', 'render-preset.json'):
            shutil.copyfile(source/name, output/name)
        references = {2: two}
        write_json(output/'report.json', report)
        print('revalidated 12 source captures; reusing candidates/control/depth-2 only', flush=True)
        for depth in (4, 8):
            references[depth] = []
            for seed in SEEDS:
                image, record = capture_reference(output, scene, preset, target, depth, seed)
                references[depth].append(image)
                report['records'].append(record)
                write_json(output/'report.json', report)
                print('captured 8192 spp b'+str(depth)+' seed '+str(seed), flush=True)
        verdict, rows = assess_scene(buffers, references[4])
        comparison = assess_depths(buffers, references)
        status = comparison['status'] if verdict == 'passed-static-glossy-fixture' and comparison[
            'status'] != 'passed-depth-agreement' else verdict
        report.update(status=status, qualityStatus=verdict, materials=rows, bounceComparison=comparison)
    except Exception as error:
        report.update(status='failed-execution', failures=[str(error)])
    report['limitation'] = 'Fixed static four-sphere fixture only. Historical candidates/control/depth-2 reused; fresh depth-4/8 references. No Cornell ROI, moving objects or subjective acceptance.'
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed-static-glossy-fixture'


if __name__ == '__main__':
    raise SystemExit(main())
