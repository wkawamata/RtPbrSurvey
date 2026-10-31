"""Exercise live NRD buffers through the existing 19-stage PT timeline."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np

from validate_history import CASES, CHANGES, validate_records
from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ('NrdReconstructedSpecular', 'NrdReconstructedDiffuse', 'NrdTiles')
ADMISSION_RESOURCES = ('NrdInputSpecular', 'NrdInputDiffuse')
RELAX_RESOURCES = ('NrdOutputSpecular', 'NrdOutputDiffuse')
COMPOSITION_RESOURCES = ('NrdSceneColor',)
ZERO_CASES = ('reset-paused-0', 'batch-4-32', 'non-accumulated-index-12', 'non-accumulated-repeat-12')


def assess_history(images, tiles=False, composed=False):
    if set(images) != set(CASES):
        raise ValueError('Missing or unexpected lifecycle checkpoints')
    for name, image in images.items():
        if (image.ndim != 3 or image.shape[-1] != (1 if tiles else 4) or image.size == 0 or
                not np.isfinite(image).all()):
            raise ValueError('Malformed lifecycle image: '+name)
        if tiles and (image != 0).any():
            raise ValueError('All-active tile mask changed')
        if composed and ((image < 0).any() or (image[..., 3] != (0 if name == 'reset-paused-0' else 1)).any()):
            raise ValueError('Invalid composed radiance or alpha: '+name)
    original_shapes = {image.shape for name, image in images.items() if not name.startswith('resize-')}
    if len(original_shapes) != 1 or images['resize-changed-16'].shape != images['resize-fresh-16'].shape:
        raise ValueError('Inconsistent lifecycle dimensions')
    zero_cases = ('reset-paused-0',) if composed else ZERO_CASES
    for name in zero_cases:
        if (images[name] != 0).any():
            raise ValueError('Reset or incompatible sampling retained reconstruction: '+name)
    comparisons = [('baseline-16', 'paused-16'), ('baseline-16', 'reset-repeat-16'), ('resumed-32', 'fresh-32')]
    comparisons += [(change+'-changed-16', change+'-fresh-16') for change in CHANGES]
    if composed:
        comparisons.append(('non-accumulated-index-12', 'non-accumulated-repeat-12'))
    for left, right in comparisons:
        if not np.array_equal(images[left], images[right]):
            raise ValueError('Lifecycle replay mismatch: '+left+' / '+right)
    if not tiles:
        if not (images['baseline-16'][..., :3] > 0).any():
            raise ValueError('No baseline reconstruction radiance')
        if np.array_equal(images['baseline-16'], images['resumed-32']):
            raise ValueError('Resume did not advance reconstruction signal')
        previous = 'baseline-16'
        for change in CHANGES:
            if not (images[change+'-changed-16'][..., :3] > 0).any():
                raise ValueError('Mutation or resize lost reconstruction signal: '+change)
            if change != 'resize' and np.array_equal(images[change+'-changed-16'], images[previous]):
                raise ValueError('Mutation did not change reconstruction signal: '+change)
            previous = change+'-fresh-16'
    return dict(exactComparisons=[dict(left=left, right=right) for left, right in comparisons],
        zeroCases=list(zero_cases), tiles=tiles, composed=composed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--shader-directory', type=Path, default=ROOT/'build/shaders')
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('--nrd-preview', action='store_true')
    parser.add_argument('--resource', action='append', choices=RESOURCES+ADMISSION_RESOURCES+RELAX_RESOURCES+COMPOSITION_RESOURCES)
    args = parser.parse_args()
    resources = args.resource or RESOURCES
    if len(set(resources)) != len(resources):
        parser.error('Duplicate lifecycle resource')
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    exe = args.exe.resolve(strict=True)
    shader_directory = args.shader_directory.resolve(strict=True)
    scene = source/'scene.json'
    preset = source/('preset.json' if (source/'preset.json').exists() else 'on.json')
    settings = json.loads(preset.read_text())
    if not settings['pathTracing']['pixelCenterPrimary'] or not settings['pathTracing']['denoiserDistanceDiagnostics']:
        parser.error('Preset must enable pixel-center primary and denoiser distance diagnostics')
    report = dict(schemaVersion=1, status='running', inputReady=False, resources=[], failures=[],
        exeSha256=sha(exe), sceneSha256=sha(scene), presetSha256=sha(preset),
        scriptSha256=sha(Path(__file__)),
        runtimeShaderSha256=sha(shader_directory/'shaders_NrdReconstruction_main.cso'))
    if any(resource in ADMISSION_RESOURCES+RELAX_RESOURCES+COMPOSITION_RESOURCES for resource in resources):
        report['admissionShaderSha256'] = sha(shader_directory/'shaders_NrdAdmission_CSMain.cso')
        report['admissionHelperSha256'] = sha(ROOT/'Shaders/NrdRelaxInputAdmission.hlsli')
        report['conversionShaderSha256'] = sha(shader_directory/'shaders_NrdConversion_CSMain.cso')
    if any(resource in RELAX_RESOURCES+COMPOSITION_RESOURCES for resource in resources):
        manifest = shader_directory/'nrd-relax-manifest.txt'
        report['sdkShaderManifestSha256'] = sha(manifest)
        report['sdkShaders'] = [dict(identifier=name, sha256=sha(shader_directory/f'nrd-relax-{i}.cso'))
            for i, name in enumerate(manifest.read_text().splitlines())]
    if any(resource in COMPOSITION_RESOURCES for resource in resources):
        report['compositionShaderSha256'] = sha(shader_directory/'shaders_NrdComposition_CSMain.cso')
        report['compositionHelperSha256'] = sha(ROOT/'Shaders/NrdRelaxComposition.hlsli')
    write_json(output/'report.json', report)
    try:
        for resource in resources:
            folder = output/resource
            folder.mkdir()
            log = folder/'runtime.log'
            command = [str(exe), '-SceneFile', str(scene), '-RenderPreset', str(preset),
                '-PathTracingHistoryValidation', str(folder), '-LogToFile', str(log),
                '-DebugPreviewResource', 'PathTracing.'+resource]
            if args.nrd_preview:
                command.append('-NrdPreview')
            entry = dict(resource=resource, command=command, status='running')
            report['resources'].append(entry)
            write_json(output/'report.json', report)
            run = subprocess.run(command, cwd=ROOT, timeout=args.timeout)
            lines = log.read_text(encoding='utf-8-sig').splitlines()
            entry['logSha256'] = sha(log)
            if run.returncode or any(line.startswith(('[ERROR]', '[CORRUPTION]', '[PathTracingHistoryFailure]')) for line in lines):
                raise ValueError('Runtime lifecycle failed: '+resource)
            if lines.count('[PathTracingHistoryComplete] captures='+str(len(CASES))) != 1:
                raise ValueError('Incomplete lifecycle: '+resource)
            records = [json.loads(line[len('[PathTracingHistory] '):]) for line in lines
                if line.startswith('[PathTracingHistory] ')]
            completed = [line[len('[PathTracingHistoryCapture] '):] for line in lines
                if line.startswith('[PathTracingHistoryCapture] ')]
            entry['records'] = records
            validate_records(records, completed)
            images = {}
            for record in records:
                path = Path(record['path'])
                meta, raw = read_buffer(path)
                width, height = record['renderWidth'], record['renderHeight']
                tiles = resource == 'NrdTiles'
                dimensions = ((width+15)//16, (height+15)//16) if tiles else (width, height)
                if (record.get('resource') != 'PathTracing.'+resource or meta['resource'] != record['resource'] or
                        meta['format'] != (41 if tiles else 2) or (meta['width'], meta['height']) != dimensions or
                        meta['sampleStartIndex'] != record['frameSampleIndex'] or meta['randomSeed'] != 11 or
                        meta['samplesPerFrame'] != record['samplesPerFrame'] or meta['primarySampling'] != 'pixel-center'):
                    raise ValueError('Invalid lifecycle capture identity or state: '+record['case'])
                images[record['case']] = raw.astype(np.float32)
                if args.nrd_preview:
                    if record['case'] == 'reset-paused-0':
                        expected_presentation = 'Native fallback: awaiting current NRD composition'
                    elif record['samplesPerFrame'] != 1:
                        expected_presentation = ('Native fallback: requires PT Radiance, 1 sample/frame, '
                            'pixel center and distance diagnostics')
                    else:
                        expected_presentation = 'NRD RELAX diagnostic (per-pixel Native fallback)'
                    if meta.get('nrdPreviewRequested') is not True or meta.get('nrdPresentation') != expected_presentation:
                        raise ValueError('Incorrect presentation after lifecycle transition: '+record['case'])
                    record['nrdPresentation'] = meta['nrdPresentation']
                record['sha256'] = sha(path)
            assessment = assess_history(images, resource == 'NrdTiles', resource in COMPOSITION_RESOURCES)
            entry.update(status='passed', assessment=assessment, d3d12Errors=0, nrdPreview=args.nrd_preview)
            write_json(output/'report.json', report)
            print('passed '+resource, flush=True)
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        if report['resources']:
            report['resources'][-1]['status'] = 'failed'
        report['failures'].append(str(error))
    report['limitation'] = ('Automated resource lifecycle on one fixture/GPU; fresh references use manual reset. '
        'Camera edits reset history in this runner. Continuous motion, visual quality and final ToneMap selection are not covered.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
