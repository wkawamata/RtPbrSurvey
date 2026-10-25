"""Bounded Native PT thin-sheet integration checks, not a general glass-quality verdict."""
import argparse
import copy
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
FALLBACK = 'Native fallback: thin transmission is not supported by NRD'


def compare_rgb(actual, expected, tolerance=0.001):
    if actual.shape != expected.shape or actual.ndim != 3 or actual.shape[2] < 3:
        raise ValueError('Invalid comparison shapes')
    if not np.isfinite(actual).all() or not np.isfinite(expected).all():
        raise ValueError('Nonfinite comparison')
    error = float(np.max(np.abs(actual[..., :3] - expected[..., :3])))
    if error > tolerance:
        raise ValueError(f'Thin transmission RGB error {error} exceeds {tolerance}')
    return error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--opaque-reference-exe', type=Path)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Output must be empty')
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'Assets/Scene/PathTracingValidation/13-nrd-stable-radiance'
    scene = json.loads((source/'scene.json').read_text(encoding='utf-8'))
    panel = json.loads((source/'panels.gltf').read_text(encoding='utf-8'))
    panel['materials'] = [panel['materials'][1]]
    panel['meshes'] = [panel['meshes'][1]]
    panel['meshes'][0]['primitives'][0]['material'] = 0
    panel['nodes'] = [dict(mesh=0)]
    panel['scenes'] = [dict(nodes=[0])]
    write_json(output/'background.gltf', panel)
    scene.update(sceneId='thin-integration', assets=[dict(id='background', type='gltf', path='background.gltf')],
        nodes=[dict(id='background', name='Background', type='gltf', assetId='background', parentId=None,
            translation=[0, 0, 0], rotation=[0, 0, 0, 1], scale=[1, 1, 1], visible=True)],
        materials=[dict(id='pane', name='Pane', baseColor=[1, 1, 1, 1], metallic=0, roughness=0.5)])
    preset = json.loads((source/'render-preset.json').read_text(encoding='utf-8'))
    preset['pathTracing'].update(nrdPreview=False, maxBounces=4, samplesPerFrame=1)
    write_json(output/'render-preset.json', preset)
    pane = dict(id='pane', name='Pane', type='primitive', materialId='pane', parentId=None,
        primitive=dict(kind='plane', width=6, depth=6), translation=[0, 0, -1],
        rotation=[0.7071067811865476, 0, 0, 0.7071067811865476], scale=[1, 1, 1], visible=True)
    report = dict(status='running', scope='thin-native-integration', qualityVerdict='not-evaluated',
        executableSha256=sha(exe), scriptSha256=sha(Path(__file__)), captures=[], checks={}, failures=[])
    write_json(output/'report.json', report)

    def capture(label, fixture, runtime=exe, nrd=False):
        folder = output/label
        folder.mkdir()
        fixture = copy.deepcopy(fixture)
        fixture['assets'][0]['path'] = '../background.gltf'
        fixture['renderPreset'] = '../render-preset.json'
        write_json(folder/'scene.json', fixture)
        path, log = folder/'SceneColor.ptbuf', folder/'debug.log'
        command = [str(runtime), '-SceneFile', str(folder/'scene.json'), '-EnablePathTracing',
            '-DebugPreviewResource', 'PathTracing.SceneColor', '-CapturePath', str(path),
            '-CaptureAfterFrames', '4', '-LogToFile', str(log), '-ExitAfterCapture']
        if nrd:
            command.append('-NrdPreview')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        child = subprocess.Popen(command, cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW, startupinfo=startup)
        try:
            code = child.wait(timeout=90)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise ValueError('Capture timeout: '+label)
        text = log.read_text(encoding='utf-8-sig')
        if code or '[ERROR]' in text or '[CORRUPTION]' in text:
            raise ValueError('Runtime/Debug Layer failure: '+label)
        meta, image = read_buffer(path)
        if meta.get('resource') != 'PathTracing.SceneColor':
            raise ValueError('Wrong captured resource')
        h, w = image.shape[:2]
        roi = image[int(h*.48):int(h*.52), int(w*.48):int(w*.52), :3]
        if not roi.size:
            raise ValueError('Empty ROI')
        report['captures'].append(dict(case=label, command=command, sceneSha256=sha(folder/'scene.json'),
            bufferSha256=sha(path), logSha256=sha(log), metadata=meta,
            roi=[int(w*.48), int(h*.48), int(w*.52), int(h*.52)]))
        return meta, image, roi

    try:
        _, opaque, background = capture('opaque-baseline', scene)
        if np.min(background) <= 0.01:
            raise ValueError('Background panel is not visible in the declared ROI')
        if args.opaque_reference_exe:
            _, previous, _ = capture('opaque-reference', scene, args.opaque_reference_exe.resolve(strict=True))
            report['checks']['opaqueRegression'] = compare_rgb(opaque, previous, 0)
        thin = copy.deepcopy(scene)
        thin['nodes'].append(pane)
        thin['materials'][0]['transmission'] = dict(mode='thin', factor=1, tint=[1, 1, 1], ior=1)
        meta, _, clear = capture('clear', thin)
        if meta.get('thinTransmissionActive') is not True:
            raise ValueError('Thin transmission was not active')
        report['checks']['clear'] = compare_rgb(clear, background)
        thin['materials'][0]['transmission']['tint'] = [0.2, 0.4, 0.8]
        _, _, colored = capture('tinted', thin)
        report['checks']['tinted'] = compare_rgb(colored, background*np.array([0.2, 0.4, 0.8]))
        thin['nodes'][-1]['rotation'] = [-pane['rotation'][0], 0, 0, pane['rotation'][3]]
        _, _, reverse = capture('backface', thin)
        report['checks']['backface'] = compare_rgb(reverse, colored)
        meta, _, fallback = capture('nrd-fallback', thin, nrd=True)
        if meta.get('nrdPresentation') != FALLBACK or meta.get('nrdLastEvaluation', {}).get('recorded') is not False:
            raise ValueError('Thin scene did not bypass NRD')
        report['checks']['nrdFallback'] = compare_rgb(fallback, colored, 0)
        thin['nodes'].append(copy.deepcopy(thin['nodes'][-1]))
        thin['nodes'][-1].update(id='pane-2', translation=[0, 0, -2])
        _, _, layers = capture('two-layers', thin)
        report['checks']['twoLayers'] = compare_rgb(layers, background*np.array([0.2, 0.4, 0.8])**2)
        thin['nodes'] = [thin['nodes'][0], thin['nodes'][1]]
        thin['materials'][0]['transmission']['factor'] = 0
        _, _, zero = capture('factor-zero', thin)
        del thin['materials'][0]['transmission']
        _, _, ordinary = capture('ordinary-pane', thin)
        report['checks']['factorZero'] = compare_rgb(zero, ordinary, 0)
        if float(np.max(np.abs(ordinary-background))) < 0.01:
            raise ValueError('Opaque endpoint control did not visibly occlude the background')
        report['status'] = 'passed-thin-native-integration'
    except Exception as error:
        report['status'] = 'failed'
        report['failures'].append(str(error))
        raise
    finally:
        write_json(output/'report.json', report)
    print(json.dumps(dict(status=report['status'], captures=len(report['captures'])), indent=2))


if __name__ == '__main__':
    main()
