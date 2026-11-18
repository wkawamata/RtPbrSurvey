"""RRQ-05: independently verify motion, lifecycle, numerical rejection and real NRD dispatch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from measure_rr_release import require_isolation
from validate_inputs import read_buffer

ROOT = Path(__file__).resolve().parents[2]
CASES = ['baseline', 'camera-translation', 'camera-stop', 'camera-rotation', 'camera-rotation-stop',
         'object-translation', 'object-stop', 'object-rotation', 'object-rotation-stop', 'projection',
         'sample-four', 'sample-one', 'jitter-primary', 'center-primary', 'scene-reload',
         'input-infinity', 'recover-infinity', 'input-huge', 'recover-huge', 'native', 'nrd', 'rr-return']


def prepare_fixture(out):
    original = ROOT / 'Assets/Scenes/PathTracingValidation/rr-quality'
    scene = json.loads((original / 'scene.json').read_text(encoding='utf-8'))
    preset = json.loads((original / 'render-preset.json').read_text(encoding='utf-8'))
    scene.update(sceneId='rrq05-single-helmet', name='RRQ05 Opaque Single Helmet', renderPreset='render-preset.json')
    scene['nodes'] = [node for node in scene['nodes'] if node['id'] == 'helmet']
    scene['nodes'][0]['translation'][0] = 0
    scene['camera']['target'][0] = 0
    for asset in scene['assets']:
        asset['path'] = Path(os.path.relpath((original / asset['path']).resolve(), out / 'fixture')).as_posix()
    preset['renderingPath'] = 2
    preset['pathTracing'].update(samplesPerFrame=1, pixelCenterPrimary=True, debugOutput=3,
                                denoiserDistanceDiagnostics=True, rrContinuous=True)
    folder = out / 'fixture'
    folder.mkdir()
    (folder / 'scene.json').write_text(json.dumps(scene, indent=2), encoding='utf-8')
    (folder / 'render-preset.json').write_text(json.dumps(preset, indent=2), encoding='utf-8')
    return folder / 'scene.json'


def validate_motion(folder, name, object_motion=False):
    metadata, motion = read_buffer(folder / (name + '-0.ptbuf'))
    position_meta, position = read_buffer(folder / (name + '-1.ptbuf'))
    key = metadata['rrRetainedInput']
    assert key == position_meta['rrRetainedInput']
    assert key['frameId'] == key['publishedFrameId']
    mask = position[..., 3] > .5
    assert mask.sum() > 100 and (~mask).sum() > 100
    current = np.asarray(key['viewProjection']).reshape(4, 4)
    previous = np.asarray(key['previousViewProjection']).reshape(4, 4)
    previous_position = position.astype(np.float64).copy()
    if object_motion:
        world = np.asarray(key['instanceWorld']).reshape(4, 4).T
        previous_world = np.asarray(key['instancePreviousWorld']).reshape(4, 4).T
        assert not np.allclose(world, previous_world, atol=1e-6)
        previous_position = position @ np.linalg.inv(world) @ previous_world
        assert np.allclose(current, previous, atol=1e-6)
        assert float(np.abs(motion[~mask]).max()) < 2e-6
    else:
        assert not np.array_equal(current, previous)
    current_clip = position @ current
    previous_clip = previous_position @ previous
    expected = (previous_clip[mask, :2] / previous_clip[mask, 3:] -
                current_clip[mask, :2] / current_clip[mask, 3:]) * [.5, -.5]
    error = float(np.abs(motion[mask, :2] - expected).max())
    magnitude = float(np.abs(expected).max())
    assert magnitude > 1e-4 and error < max(3e-5, magnitude*.001), (name, error, magnitude)
    if not object_motion:
        height, width = mask.shape
        y, x = np.mgrid[:height, :width]
        far = np.stack(((x+.5)*2/width-1, 1-(y+.5)*2/height, np.ones_like(x), np.ones_like(x)), axis=-1)
        near = far.copy()
        near[..., 2] = 0
        inv = np.linalg.inv(current)
        a, b = far @ inv, near @ inv
        sky = np.concatenate((a[..., :3]/a[..., 3:] - b[..., :3]/b[..., 3:],
                              np.zeros((*mask.shape, 1))), axis=-1)
        c, p = sky @ current, sky @ previous
        sky_expected = (p[~mask, :2]/p[~mask, 3:] - c[~mask, :2]/c[~mask, 3:]) * [.5, -.5]
        sky_error = float(np.abs(motion[~mask, :2] - sky_expected).max())
        assert sky_error < 3e-5, (name, sky_error)
    else:
        sky_error = 0
    return dict(case=name, surfacePixels=int(mask.sum()), maxMotionUv=magnitude,
                maxErrorUv=error, skyErrorUv=sky_error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if not args.prepare_only and args.exe is None:
        parser.error('--exe is required for GPU validation')
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('Output must be empty')
    if not args.prepare_only:
        require_isolation()
    out.mkdir(parents=True, exist_ok=True)
    fixture = prepare_fixture(out)
    report = dict(status='prepared', fixtureSha256=hashlib.sha256(fixture.read_bytes()).hexdigest())
    try:
        if not args.prepare_only:
            exe = args.exe.resolve(strict=True)
            report['executableSha256'] = hashlib.sha256(exe.read_bytes()).hexdigest()
            command = [str(exe), '-SceneFile', str(fixture), '-WindowSize', '320', '180',
                       '-EnableExperimentalNativeRayReconstruction', '-LogToFile', str(out/'d3d12.log'),
                       '-PathTracingRrRegressionValidation', str(out)]
            report['command'] = command
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
            try:
                code = child.wait(timeout=600)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
            report['exitCode'] = code
            require_isolation()
            text = (out/'d3d12.log').read_text(encoding='utf-8-sig')
            assert code == 0 and '[RrRegressionComplete] passed=true' in text, text[-4000:]
            assert not any(word in text for word in ['[ERROR]', '[CORRUPTION]', '[RrRegressionFailure]'])
            rows = [json.loads(line[len('[RrRegression] '):]) for line in text.splitlines()
                    if line.startswith('[RrRegression] ')]
            assert [r['case'] for r in rows] == CASES
            by_name = {r['case']: r for r in rows}
            for name in CASES:
                metadata, output = read_buffer(out/(name+'-4.ptbuf'))
                assert np.isfinite(output[..., :3]).all() and output[..., :3].std() > .001
                if name == 'nrd':
                    actual = metadata['nrdLastEvaluation']
                    assert actual['recorded'] and actual['dispatchCount'] > 0
                    assert by_name[name]['nrdRecorded'] and by_name[name]['nrdDispatches'] > 0
                    assert by_name[name]['output'] == 'nrd'
            motions = [validate_motion(out, name, name.startswith('object')) for name in
                       ['camera-translation', 'camera-rotation', 'object-translation', 'object-rotation']]
            for name in ['camera-stop', 'camera-rotation-stop', 'object-stop', 'object-rotation-stop']:
                _, motion = read_buffer(out/(name+'-0.ptbuf'))
                assert float(np.abs(motion).max()) < 3e-6, name
            for previous, fault in [('scene-reload', 'input-infinity'), ('recover-infinity', 'input-huge')]:
                _, a = read_buffer(out/(previous+'-4.ptbuf'))
                _, b = read_buffer(out/(fault+'-4.ptbuf'))
                assert a.tobytes() == b.tobytes(), fault
            assert by_name['sample-four']['samples'] == 4 and by_name['sample-one']['samples'] == 1
            assert not by_name['jitter-primary']['pixelCenter'] and by_name['center-primary']['pixelCenter']
            report.update(status='passed', checkpoints=rows, motion=motions, d3d12Errors=0)
            print('RR regression passed:', len(rows), 'checkpoints; 4 independent motion checks; actual NRD dispatch')
    except Exception as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        (out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
