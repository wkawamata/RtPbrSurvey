"""Numerically verify retained object motion and continuous RR pause/resume."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from validate_inputs import read_buffer


def run(exe, output, full_scene=False, size=(320, 180)):
    root = Path(__file__).resolve().parents[2]
    output.mkdir(parents=True, exist_ok=False)
    records = []
    executable_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    for kind in ('Rotation', 'Translation'):
        source = root / f'Assets/Scenes/RrObject{kind}'
        folder = output / kind.lower()
        folder.mkdir()
        scene = json.loads((source / 'scene.json').read_text(encoding='utf-8'))
        if not full_scene:
            scene['nodes'] = [n for n in scene['nodes'] if n['id'] == 'helmet']
        scene['renderPreset'] = os.path.relpath(source / scene['renderPreset'], folder).replace('\\', '/')
        for asset in scene['assets']:
            asset['path'] = os.path.relpath(source / asset['path'], folder).replace('\\', '/')
        (folder / 'scene.json').write_text(json.dumps(scene), encoding='utf-8')
        command = [str(exe), '-SceneFile', str(folder / 'scene.json'),
                   '-PathTracingReconstruction', 'rr', '-EnableDlssRayReconstruction',
                   '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrContinuous',
                   '-WindowSize', str(size[0]), str(size[1]), '-PathTracingRrLifecycleValidation', str(folder),
                   '-LogToFile', str(folder / 'runtime.log')]
        code = subprocess.run(command, cwd=root, timeout=180).returncode
        text = (folder / 'runtime.log').read_text(encoding='utf-8-sig')
        assert code == 0 and '[PathTracingRrObjectComplete] checkpoints=5' in text, text[-3000:]
        assert not any(l.startswith(('[ERROR]', '[CORRUPTION]')) for l in text.splitlines())
        metadata, motion = read_buffer(folder / 'object-motion.ptbuf')
        position_metadata, position = read_buffer(folder / 'object-position.ptbuf')
        key = metadata['rrRetainedInput']
        assert position_metadata['rrRetainedInput'] == key
        mask = position[..., 3] > .5
        actual = motion[..., :2]
        assert mask.sum() > 100 and np.isfinite(position).all() and np.isfinite(actual).all()
        assert np.max(np.abs(actual[mask])) > 1e-4
        assert np.allclose(key['viewProjection'], key['previousViewProjection'], atol=1e-6)
        background_drift = float(np.max(np.abs(actual[~mask]))) if (~mask).any() else 0.0
        output_name = 'object-output-alternate.ptbuf' if key['publishedSlot'] else 'object-output.ptbuf'
        output_metadata, output_image = read_buffer(folder / output_name)
        assert output_metadata['rrRetainedInput'] == key
        assert np.isfinite(output_image).all() and output_image[..., :3].std() > 1e-3
        error = None
        if not full_scene:
            world = np.asarray(key['instanceWorld']).reshape(4, 4).T
            previous_world = np.asarray(key['instancePreviousWorld']).reshape(4, 4).T
            assert not np.allclose(world, previous_world, atol=1e-6)
            current_vp = np.asarray(key['viewProjection']).reshape(4, 4)
            previous_vp = np.asarray(key['previousViewProjection']).reshape(4, 4)
            previous_position = position @ np.linalg.inv(world) @ previous_world
            current_clip = position @ current_vp
            previous_clip = previous_position @ previous_vp
            mask = position[..., 3] > .5
            assert mask.sum() > 100
            expected = (previous_clip[mask][..., :2] / previous_clip[mask][..., 3:] -
                        current_clip[mask][..., :2] / current_clip[mask][..., 3:]) * [.5, -.5]
            actual = motion[..., :2]
            assert np.isfinite(actual).all()
            assert np.max(np.abs(expected)) > 1e-4
            assert np.allclose(actual[mask], expected, atol=3e-5, rtol=1e-3), np.max(np.abs(actual[mask]-expected))
            error = float(np.max(np.abs(actual[mask]-expected)))
        checkpoints = [json.loads(l[len('[PathTracingRrLifecycle] '):]) for l in text.splitlines()
                       if l.startswith('[PathTracingRrLifecycle] {')]
        records.append(dict(scene=kind, status='passed', command=command, executableSha256=executable_hash,
                            fullScene=full_scene, maxMotionError=error,
                            backgroundStationaryCheckPassed=background_drift < 2e-6,
                            backgroundDriftUv=background_drift,
                            maxMotionUv=float(np.max(np.abs(actual[mask]))),
                            outputStd=float(output_image[..., :3].std()),
                            surfacePixels=int(mask.sum()), checkpoints=checkpoints))
        (output / 'report.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        print(kind, 'object checks passed', error, 'background check', background_drift < 2e-6, background_drift, flush=True)
    (output / 'report.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    assert all(r['backgroundStationaryCheckPassed'] for r in records), 'Stationary background motion exceeded 2e-6 UV'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--full-scene', action='store_true')
    parser.add_argument('--size', nargs=2, type=int, default=(320, 180), metavar=('WIDTH', 'HEIGHT'))
    args = parser.parse_args()
    if any(value < 64 or value > 4096 for value in args.size):
        parser.error('Size must be in [64,4096]')
    run(args.exe.resolve(strict=True), args.output.resolve(), args.full_scene, args.size)
