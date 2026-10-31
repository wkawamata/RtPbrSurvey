"""Validate continuous NRD history through camera reversal and settling checkpoints."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np

from validate_inputs import read_buffer
from validate_nrd_camera_motion import assess_frame, assess_motion
from validate_nrd_disocclusion import (ROOT, FIXTURE, RESOURCES, check_cohort,
    assess_visibility, visibility_regions, measure_color)
from validate_part1 import sha, write_json

CHECKPOINTS = (('warm', 20, False, 20), ('forward', 36, True, 0),
    ('reverse', 44, True, 0), ('middle', 60, True, 0), ('stop', 80, True, 0),
    ('settle-4', 84, False, 4), ('settle-16', 96, False, 16), ('settle-32', 112, False, 32))
KEYFRAMES = ((0, 0), (20, 0), (40, 20), (80, -20), (120, -20))


def make_plan():
    return dict(version=1, cameraKeyframes=[dict(frame=frame, yawDegrees=yaw) for frame, yaw in KEYFRAMES],
        captures=[dict(frame=frame, caseId=case, path=case+'-{variant}.ptbuf')
            for case, frame, _, _ in CHECKPOINTS])


def check_scheduled(records, folder):
    if len(records) != len(CHECKPOINTS):
        raise ValueError('Incomplete scheduled timeline')
    for record, (case, frame, _, _) in zip(records, CHECKPOINTS):
        if (record.get('case') != case or record.get('frame') != frame or
                Path(record.get('path', '')).resolve() != folder/(case+'-pt.ptbuf')):
            raise ValueError('Duplicate, reordered or wrong timeline checkpoint')


def check_frame(meta, frame, moving, sample_index):
    assess_frame(meta, True, moving)
    if (meta['nrdLastEvaluation']['frameIndex'] != frame or
            meta.get('sampleStartIndex') != sample_index or meta.get('samplesPerFrame') != 1 or
            meta.get('primarySampling') != 'pixel-center'):
        raise ValueError('SDK timeline or Native reset/accumulation clock differs')


def capture_timeline(folder, resource):
    folder.mkdir(parents=True)
    plan, log = folder/'plan.json', folder/'runtime.log'
    write_json(plan, make_plan())
    command = [str(ROOT/'build/Debug/RtPbrSurvey.exe'), '-SceneFile', str(FIXTURE/'scene.json'),
        '-RenderPreset', str(FIXTURE/'render-preset.json'), '-PathTracingCapturePlan', str(plan),
        '-DebugPreviewResource', 'PathTracing.'+resource, '-NrdContinuousCameraHistory',
        '-LogToFile', str(log), '-ExitAfterCapture']
    child = subprocess.Popen(command, cwd=ROOT)
    try:
        code = child.wait(timeout=300)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    text = log.read_text(encoding='utf-8-sig')
    if code != 0 or any(token in text for token in ('[ERROR]', '[CORRUPTION]')):
        raise ValueError('Timeline runtime failure: '+resource)
    scheduled = [json.loads(line[len('[PathTracingTimeline] '):]) for line in text.splitlines()
        if line.startswith('[PathTracingTimeline] ')]
    check_scheduled(scheduled, folder)
    completed = [line for line in text.splitlines() if line.startswith('[Capture] ')]
    if (len(completed) != len(CHECKPOINTS) or 'complete=true' not in completed[-1] or
            any('complete=false' not in line for line in completed[:-1])):
        raise ValueError('Incomplete or incorrect timeline output drain')
    return dict(resource=resource, command=command, logSha256=sha(log), planSha256=sha(plan),
        scheduled=scheduled, d3d12Errors=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.output.resolve()
    if folder == FIXTURE or FIXTURE in folder.parents or (folder.exists() and any(folder.iterdir())):
        parser.error('Output must be a separate empty directory')
    scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))
    manifest = ROOT/'build/shaders/nrd-relax-manifest.txt'
    report = dict(status='running', qualityVerdict='not-evaluated', fullIntegrationReady=False,
        records=[], runs=[], cases=[], failures=[], executableSha256=sha(ROOT/'build/Debug/RtPbrSurvey.exe'),
        scriptSha256=sha(Path(__file__)), fixtureSha256={name: sha(FIXTURE/name) for name in
            ('scene.json', 'render-preset.json')},
        helperSha256={name: sha(Path(__file__).parent/name) for name in
            ('validate_nrd_disocclusion.py', 'validate_nrd_camera_motion.py', 'validate_secondary_geometry.py', 'validate_inputs.py')},
        shaderManifestSha256=sha(manifest), sdkShaderSha256={name: sha(ROOT/f'build/shaders/nrd-relax-{index}.cso')
            for index, name in enumerate(manifest.read_text().splitlines())},
        shaderSha256={name: sha(ROOT/'build/shaders'/name) for name in
            ('shaders_PathTracing_CSMain.cso', 'shaders_NrdConversion_CSMain.cso', 'shaders_NrdComposition_CSMain.cso')})
    write_json(folder/'report.json', report)
    try:
        for resource in RESOURCES:
            report['runs'].append(capture_timeline(folder/resource, resource))
            write_json(folder/'report.json', report)
            print('captured timeline '+resource, flush=True)
        cameras, motion_x = {}, {}
        stop = None
        for case, frame, moving, sample_index in CHECKPOINTS:
            common, buffers, metadata = None, {}, {}
            for resource in RESOURCES:
                path = folder/resource/(case+'-pt.ptbuf')
                meta, image = read_buffer(path)
                common = check_cohort(meta, resource, common)
                check_frame(meta, frame, moving, sample_index)
                metadata[resource], buffers[resource] = meta, image.astype(np.float32)
                report['records'].append(dict(case=case, frame=frame, resource=resource, path=str(path), sha256=sha(path)))
            meta = metadata['Albedo']
            entry = dict(case=case, frame=frame, metadata=common,
                visibility=assess_visibility(meta, buffers['Albedo'], scene, moving),
                motion=assess_motion(metadata['MotionVectors'], buffers['MotionVectors'], scene, moving),
                colorMeasurements=measure_color(meta, scene, buffers['NrdSceneColor'], buffers['SceneColor']))
            cameras[case] = np.asarray(meta['viewProjection'])
            motion_x[case] = entry['motion']['observedUvMean'][0]
            if case == 'stop':
                xs, ys, _, _, revealed, retained = visibility_regions(meta, scene)
                stop = dict(xs=xs, ys=ys, revealed=revealed, retained=retained,
                    color=buffers['NrdSceneColor'][ys, xs, :3].copy())
            if case.startswith('settle-'):
                if not np.allclose(cameras[case], cameras['stop'], atol=2e-6, rtol=2e-6):
                    raise ValueError('Camera changed during settling')
                color = buffers['NrdSceneColor'][stop['ys'], stop['xs'], :3]
                entry['originalStopRegions'] = {}
                for name in ('revealed', 'retained'):
                    mask = stop[name]
                    values = color[mask]
                    entry['originalStopRegions'][name] = dict(sampledPixels=int(mask.sum()),
                        nrdMeanRgb=values.mean(axis=0).tolist(),
                        rmsChangeFromStop=float(np.sqrt(np.mean((values-stop['color'][mask])**2))))
                if case == 'settle-32' and int(np.any(color != stop['color'], axis=-1).sum()) < 100:
                    raise ValueError('Settling did not update the NRD signal')
            report['cases'].append(entry)
            write_json(folder/'report.json', report)
        if (not np.allclose(cameras['forward'], cameras['reverse'], atol=2e-6, rtol=2e-6) or
                motion_x['forward']*motion_x['reverse'] >= 0):
            raise ValueError('Forward/reverse did not revisit one pose with opposite motion')
        report['status'] = 'passed'
    except Exception as error:
        report.update(status='failed', failures=[str(error)])
    report['limitation'] = 'Four separate deterministic resource runs, each continuous across all eight checkpoints. Input/history/finite-output qualification, not a high-sample or subjective reversal/settling quality gate. No moving objects or camera cuts.'
    write_json(folder/'report.json', report)
    print(report['status'], report['failures'], flush=True)
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
