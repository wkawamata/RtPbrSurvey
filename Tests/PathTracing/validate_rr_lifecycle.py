"""Exercise continuous RR camera motion, pause/resume, switching and resize in one GPU process."""
import argparse
import base64
import io
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from validate_part1 import sha, write_json
from validate_rr_native import fixture
from validate_inputs import read_buffer


def motion_fixture(output):
    # A spatial pattern makes retained radiance/output changes observable under
    # translation and rotation, unlike the original uniform emissive enclosure.
    y, x = np.mgrid[:128, :128]
    pixels = np.stack((32 + x * 223 // 127, 32 + y * 223 // 127,
                       48 + ((x // 16 + y // 16) % 2) * 160), axis=-1).astype('uint8')
    data = io.BytesIO()
    Image.fromarray(pixels).save(data, format='PNG')
    gltf = json.loads((output/'box.gltf').read_text())
    gltf['images'].append(dict(uri='data:image/png;base64,'+base64.b64encode(data.getvalue()).decode()))
    gltf['textures'].append(dict(source=1))
    gltf['materials'][1]['emissiveTexture']['index'] = 1
    write_json(output/'box.gltf', gltf)


def check_motion_capture(output, prefix):
    motion_meta, motion = read_buffer(output/f'{prefix}-0.ptbuf')
    key = motion_meta['rrRetainedInput']
    arrays = {}
    for index in (1, 2, 3, 4 if key['publishedSlot'] == 1 else 5):
        meta, values = read_buffer(output/f'{prefix}-{index}.ptbuf')
        if meta['rrRetainedInput'] != key:
            raise ValueError('Motion capture mixed retained productions')
        arrays[index] = values
    if key['frameId'] != key['publishedFrameId']:
        raise ValueError('Motion capture was not the verified production')
    current = np.asarray(key['viewProjection']).reshape(4, 4)
    previous = np.asarray(key['previousViewProjection']).reshape(4, 4)
    if np.array_equal(current, previous):
        raise ValueError('Moving production used identical camera transforms')
    position = arrays[1]
    background = position[..., 3] == 0
    height, width = position.shape[:2]
    if background.any():
        y, x = np.mgrid[:height, :width]
        ndc = np.stack(((x+.5)*2/width-1, 1-(y+.5)*2/height,
                        np.ones_like(x), np.ones_like(x)), axis=-1)
        near = ndc.copy()
        near[..., 2] = 0
        inverse = np.linalg.inv(current)
        far_world, near_world = ndc @ inverse, near @ inverse
        direction = far_world[..., :3]/far_world[..., 3:] - near_world[..., :3]/near_world[..., 3:]
        sky = np.concatenate((direction, np.zeros((height, width, 1))), axis=-1)
        position = np.where(background[..., None], sky, position)
    current_clip, previous_clip = position @ current, position @ previous
    expected = (previous_clip[..., :2] / previous_clip[..., 3:] -
                current_clip[..., :2] / current_clip[..., 3:]) * [.5, -.5]
    # PT raw motion is RG16F before the float32 RR conversion. Account for that
    # quantization, and independently verify sign, scale and frame reference.
    error = float(np.abs(motion - expected).max())
    magnitude = float(np.abs(expected).max())
    if magnitude < 1e-4 or error > max(2e-6, magnitude * .001):
        raise ValueError(f'Motion reprojection mismatch: max={magnitude}, error={error}')
    background_magnitude = float(np.abs(expected[background]).max()) if background.any() else 0
    background_error = float(np.abs(motion[background]-expected[background]).max()) if background.any() else 0
    if background.any() and ((prefix == 'translation' and background_magnitude > 1e-6) or
                             (prefix == 'rotation' and background_magnitude < 1e-4) or
                             background_error > max(2e-6, background_magnitude * .001)):
        raise ValueError('Background motion lost its infinity/rotation convention')
    clip_to_previous = np.asarray(key['clipToPrevClip']).reshape(4, 4)
    previous_to_clip = np.asarray(key['prevClipToClip']).reshape(4, 4)
    if not np.allclose(clip_to_previous, np.linalg.inv(current) @ previous, atol=2e-6, rtol=2e-6):
        raise ValueError('SDK clip transform differs from retained motion reference')
    if not np.allclose(clip_to_previous @ previous_to_clip, np.eye(4), atol=2e-6):
        raise ValueError('SDK inverse clip transform mismatch')
    world_to_view = np.asarray(key['worldToView']).reshape(4, 4)
    if not np.allclose(world_to_view @ np.asarray(key['viewToClip']).reshape(4, 4), current, atol=2e-6):
        raise ValueError('Specular hit-distance camera does not match input')
    if not np.allclose(world_to_view @ np.asarray(key['viewToWorld']).reshape(4, 4), np.eye(4), atol=2e-6):
        raise ValueError('Specular camera inverse mismatch')
    if not np.array_equal(arrays[2], arrays[3]):
        raise ValueError('RR color input changed scene-referred radiance')
    result = arrays[4 if key['publishedSlot'] == 1 else 5][..., :3]
    if result.max() <= 0 or float(result.std()) < .01 or float(arrays[2][..., :3].std()) < .01:
        raise ValueError('Patterned moving image is black or uniform')
    return dict(frameId=key['frameId'], backgroundPixels=int(background.sum()), backgroundMotionUv=background_magnitude,
                backgroundMotionError=background_error, maxMotionUv=magnitude, maxMotionError=error,
                inputSha256=sha(output/f'{prefix}-2.ptbuf'), outputStd=float(result.std()),
                currentInputRmse=float(np.sqrt(np.mean((result-arrays[2][..., :3])**2))))


def check_image_change(output):
    images = []
    for prefix in ('translation', 'rotation'):
        meta, radiance = read_buffer(output/f'{prefix}-2.ptbuf')
        slot = meta['rrRetainedInput']['publishedSlot']
        _, result = read_buffer(output/f'{prefix}-{4 if slot else 5}.ptbuf')
        images.append((radiance[..., :3], result[..., :3]))
    changes = dict(inputMeanAbsoluteChange=float(np.abs(images[1][0]-images[0][0]).mean()),
                   outputMeanAbsoluteChange=float(np.abs(images[1][1]-images[0][1]).mean()))
    if min(changes.values()) < 1e-4:
        raise ValueError('Moving camera did not change input and verified output pixels')
    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--general-background', action='store_true')
    args = parser.parse_args()
    output, source, exe = args.output.resolve(), args.source.resolve(strict=True), args.exe.resolve(strict=True)
    if output == source or source in output.parents or (output.exists() and any(output.iterdir())):
        parser.error('Output must be a separate empty directory')
    fixture(source, output)
    motion_fixture(output)
    if args.general_background:
        gltf = json.loads((output/'box.gltf').read_text(encoding='utf-8'))
        gltf['scenes'][0]['nodes'] = [1]
        gltf['nodes'][1]['scale'] = [.3, .3, 1]
        gltf['materials'][0]['pbrMetallicRoughness'].update(metallicFactor=.5, roughnessFactor=.5)
        gltf['materials'][0]['emissiveFactor'] = [.2, .1, .05]
        gltf['materials'][0]['emissiveTexture'] = dict(index=1)
        write_json(output/'box.gltf', gltf)
        preset = json.loads((output/'preset.json').read_text(encoding='utf-8'))
        preset['pathTracing'].update(environmentEnabled=True, environmentSamplingMode=5)
        preset['lighting']['iblIntensity'] = 1
        write_json(output/'preset.json', preset)
    log = output/'runtime.log'
    command = [str(exe), '-SceneFile', str(output/'scene.json'), '-RenderPreset', str(output/'preset.json'),
               '-WindowSize', '256', '144', '-EnableDlssRayReconstruction',
               '-EnableExperimentalNativeRayReconstruction', '-PathTracingRrLifecycleValidation', str(output),
               '-LogToFile', str(log)]
    report = dict(status='running', executableSha256=sha(exe),
                  conversionShaderSha256=sha(exe.parent/'shaders_PathTracingRrConversion_CSMain.cso'),
                  producerShaderSha256=sha(exe.parent/'shaders_PathTracing_CSMain.cso'), command=command, checkpoints=[], failures=[])
    write_json(output/'report.json', report)
    try:
        child = subprocess.Popen(command, cwd=Path(__file__).resolve().parents[2])
        try:
            code = child.wait(timeout=180)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
        report['exitCode'] = code
        text = log.read_text(encoding='utf-8-sig')
        errors = [line for line in text.splitlines()
                  if line.startswith(('[ERROR]', '[CORRUPTION]', '[PathTracingRrLifecycleFailure]'))]
        if code != 0 or errors:
            raise ValueError(f'Process={code}, errors={errors}')
        prefix = '[PathTracingRrLifecycle] '
        records = [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]
        expected = ['continuous', 'paused', 'resumed', 'native', 'nrd', 'rr-restored', 'pending-reset',
                    'camera-translation', 'camera-rotation', 'camera-stopped', 'projection-changed',
                    'resized', 'resize-restored']
        expected[10:10] = ['input-rejected', 'input-recovered']
        if [item['case'] for item in records] != expected or '[PathTracingRrLifecycleComplete] checkpoints=15' not in text:
            raise ValueError('Incomplete lifecycle checkpoints')
        for item in records:
            if item['case'] not in ('native', 'nrd', 'input-rejected') and (item['output'] != 'rr' or not item['verified']):
                raise ValueError('RR checkpoint did not publish verified output')
        if records[0]['continuous'] < 2 or records[0]['resets'] > 1 + records[0]['inputRejected']:
            raise ValueError('Continuous history was not exercised')
        if (records[2]['resets'] > records[1]['resets'] + records[2]['inputRejected'] - records[1]['inputRejected'] or
                records[2]['lastReset'] or records[2]['cancelled'] != records[1]['cancelled'] or
                records[2]['publishedFrameId'] <= records[1]['publishedFrameId']):
            raise ValueError('Resume reset without an input rejection or retained stale output')
        if records[1]['nativeCalls'] != records[0]['nativeCalls'] or records[1]['publishedFrameId'] != records[0]['publishedFrameId']:
            raise ValueError('Paused production was re-evaluated')
        if records[3]['output'] != 'native' or records[4]['output'] != 'nrd':
            raise ValueError('Backend switch was not reflected in presentation')
        if records[6]['cancelled'] <= records[5]['cancelled']:
            raise ValueError('In-flight cancellation was not exercised')
        for index in (7, 8, 9):
            if (records[index]['resets'] > records[6]['resets'] + records[index]['inputRejected'] - records[6]['inputRejected'] or
                    records[index]['cancelled'] != records[6]['cancelled'] or records[index]['lastReset'] or
                    records[index]['publishedFrameId'] <= records[index-1]['publishedFrameId']):
                raise ValueError('Camera motion/stop did not preserve advancing RR history')
        if (records[10]['inputRejected'] <= records[9]['inputRejected'] or records[10]['output'] != 'native' or
                records[10]['verified'] or records[11]['resets'] <= records[10]['resets']):
            raise ValueError('Unsupported input did not fall back and recover with reset history')
        if records[12]['resets'] <= records[11]['resets']:
            raise ValueError('Projection change did not reset RR')
        if (records[13]['width'], records[13]['height']) != (320, 180) or (records[14]['width'], records[14]['height']) != (256, 144):
            raise ValueError('Resize output dimensions did not update')
        motion_results = {prefix: check_motion_capture(output, prefix) for prefix in ('translation', 'rotation')}
        if args.general_background and not all(item['backgroundPixels'] for item in motion_results.values()):
            raise ValueError('Moving capture did not include background pixels')
        image_change = check_image_change(output)
        image = output/'viewport.png'
        if not image.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Final verified viewport capture missing')
        report.update(status='passed', checkpoints=records, motion=motion_results,
                      imageChange=image_change, d3d12Errors=0, viewportSha256=sha(image))
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    write_json(output/'report.json', report)
    print(json.dumps(report), flush=True)
    if report['status'] != 'passed':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
