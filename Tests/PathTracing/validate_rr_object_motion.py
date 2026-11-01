"""Numerically verify retained object motion and continuous RR pause/resume."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from validate_inputs import read_buffer


def run(exe, output):
    root = Path(__file__).resolve().parents[2]
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for kind in ('Rotation', 'Translation'):
        source = root / f'Assets/Scenes/RrObject{kind}'
        folder = output / kind.lower()
        folder.mkdir()
        scene = json.loads((source / 'scene.json').read_text(encoding='utf-8'))
        scene['nodes'] = [n for n in scene['nodes'] if n['id'] == 'helmet']
        scene['renderPreset'] = os.path.relpath(source / scene['renderPreset'], folder).replace('\\', '/')
        for asset in scene['assets']:
            asset['path'] = os.path.relpath(source / asset['path'], folder).replace('\\', '/')
        (folder / 'scene.json').write_text(json.dumps(scene), encoding='utf-8')
        command = [str(exe), '-SceneFile', str(folder / 'scene.json'),
                   '-PathTracingReconstruction', 'rr', '-EnableDlssRayReconstruction',
                   '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrContinuous',
                   '-WindowSize', '320', '180', '-PathTracingRrLifecycleValidation', str(folder),
                   '-LogToFile', str(folder / 'runtime.log')]
        code = subprocess.run(command, cwd=root, timeout=180).returncode
        text = (folder / 'runtime.log').read_text(encoding='utf-8-sig')
        assert code == 0 and '[PathTracingRrObjectComplete] checkpoints=5' in text, text[-3000:]
        assert not any(l.startswith(('[ERROR]', '[CORRUPTION]')) for l in text.splitlines())
        metadata, motion = read_buffer(folder / 'object-motion.ptbuf')
        position_metadata, position = read_buffer(folder / 'object-position.ptbuf')
        key = metadata['rrRetainedInput']
        assert position_metadata['rrRetainedInput'] == key
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
        checkpoints = [json.loads(l[len('[PathTracingRrLifecycle] '):]) for l in text.splitlines()
                       if l.startswith('[PathTracingRrLifecycle] {')]
        records.append(dict(scene=kind, status='passed', command=command,
                            maxMotionError=float(np.max(np.abs(actual[mask]-expected))),
                            surfacePixels=int(mask.sum()), checkpoints=checkpoints))
        print(kind, 'passed', records[-1]['maxMotionError'], flush=True)
    (output / 'report.json').write_text(json.dumps(records, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.exe.resolve(strict=True), args.output.resolve())
