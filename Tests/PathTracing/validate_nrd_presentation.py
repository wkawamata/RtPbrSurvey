"""Capture main-viewport NRD A/B and incompatible-input Native fallbacks."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
RELAX = 'NRD RELAX diagnostic (per-pixel Native fallback)'
INCOMPATIBLE = ('Native fallback: requires PT Radiance, 1 sample/frame, '
    'pixel center and distance diagnostics')
VARIANTS = {
    'qualified': {},
    'distance-off': dict(denoiserDistanceDiagnostics=False),
    'random-primary': dict(pixelCenterPrimary=False),
    'batch-4': dict(samplesPerFrame=4),
    'normal-view': dict(debugOutput=1),
}


def assess_pair(native, preview, qualified):
    if native.shape != preview.shape or native.ndim != 3 or native.shape[2] != 3:
        raise ValueError('Mismatched final-output images')
    height, width = native.shape[:2]
    # Exclude the left Debug and right Evaluation Cases windows, including dynamic timing text.
    box = (int(width*.40), int(height*.25), int(width*.65), int(height*.75))
    x0, y0, x1, y1 = box
    a, b = native[y0:y1, x0:x1], preview[y0:y1, x0:x1]
    if a.size == 0:
        raise ValueError('Empty final-output comparison region')
    changed = int(np.any(a != b, axis=-1).sum())
    if (qualified and changed < 100) or (not qualified and changed != 0):
        raise ValueError('Main-viewport A/B or exact Native fallback failed')
    return dict(region=list(box), changedPixels=changed, exactNativeFallback=not qualified)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--without-sdk', action='store_true')
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if source == output or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    scene = json.loads((source/'scene.json').read_text())
    scene['materials'][0]['metallic'] = 1.0
    preset = json.loads((source/('preset.json' if (source/'preset.json').exists() else 'on.json')).read_text())
    preset['pathTracing'].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True,
        denoiserDistanceDiagnostics=True, debugOutput=3, nrdPreview=False)
    write_json(output/'scene.json', scene)
    exe = args.exe.resolve(strict=True)
    report = dict(status='running', fullIntegrationReady=False, cases=[], failures=[],
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)), sceneSha256=sha(output/'scene.json'),
        sdkDiagnosticsBuilt=not args.without_sdk,
        toneMapShaderSha256=sha(exe.parent/'shaders_ToneMap_PSMain.cso'))
    write_json(output/'report.json', report)
    try:
        variants = {'qualified': {}} if args.without_sdk else VARIANTS
        for name, overrides in variants.items():
            entry = dict(case=name, captures=[])
            report['cases'].append(entry)
            images = []
            for enabled in (False, True):
                label = name+('-preview' if enabled else '-native')
                settings = copy.deepcopy(preset)
                settings['pathTracing'].update(overrides, nrdPreview=False)
                preset_path, path, log = (output/(label+ext) for ext in ('.json', '.png', '.log'))
                write_json(preset_path, settings)
                command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(preset_path),
                    '-EnablePathTracing', '-CapturePath', str(path), '-CaptureAfterFrames', '30',
                    '-LogToFile', str(log), '-ExitAfterCapture']
                if enabled:
                    command.append('-NrdPreview')
                run = subprocess.run(command, cwd=ROOT, timeout=180)
                lines = log.read_text(encoding='utf-8-sig').splitlines()
                if run.returncode or any(line.startswith(('[ERROR]', '[CORRUPTION]')) for line in lines):
                    raise ValueError('Runtime or D3D12 failure: '+label)
                records = [json.loads(line[len('[PathTracing] '):]) for line in lines
                    if line.startswith('[PathTracing] ')]
                expected = 'Native PT' if not enabled else RELAX if name == 'qualified' else INCOMPATIBLE
                if enabled and args.without_sdk:
                    expected = 'Native fallback: NRD SDK diagnostics not built'
                if len(records) != 1 or records[0].get('nrdPresentation') != expected or records[0].get('nrdPreviewRequested') is not enabled:
                    raise ValueError('Missing or incorrect presentation diagnostics: '+label)
                entry['captures'].append(dict(command=command, path=str(path), sha256=sha(path),
                    logSha256=sha(log), presetSha256=sha(preset_path), diagnostics=records[0], d3d12Errors=0))
                images.append(np.array(Image.open(path).convert('RGB')))
                write_json(output/'report.json', report)
            entry['assessment'] = assess_pair(*images, qualified=name == 'qualified' and not args.without_sdk)
            print('passed '+name, entry['assessment'], flush=True)
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = ('Fixed central viewport ROI excludes UI. Confirms final-output source selection and '
        'exact Native fallback, not denoising quality or SDK/device failure recovery.')
    write_json(output/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
