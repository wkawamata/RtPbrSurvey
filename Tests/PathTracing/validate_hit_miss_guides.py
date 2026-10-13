"""Validate first-sample guides and all-sample signals across a finite-plane silhouette."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import numpy as np
from make_boundary_fixture import BACKGROUND
from validate_inputs import ROOT, plane_hits, project, read_buffer, srgb_texture_material
from validate_guide_contract import RESOURCES, FORMATS, compare_primary_guides
from validate_part1 import sha, write_json


def sample_hits(meta, xs, ys, batch):
    hits = []
    for offset in range(batch):
        sample_meta = dict(meta, sampleStartIndex=meta['sampleStartIndex']+offset)
        world, _, _ = plane_hits(sample_meta, xs, ys)
        hits.append((abs(world[:, 0]) < 1) & (abs(world[:, 1]) < 1))
    return np.asarray(hits)


def uncertain_sample_hits(meta, xs, ys, batch):
    uncertain = []
    for offset in range(batch):
        sample_meta = dict(meta, sampleStartIndex=meta['sampleStartIndex']+offset)
        world, _, _ = plane_hits(sample_meta, xs, ys)
        margin = 8*np.finfo(np.float32).eps*np.maximum(1, abs(world[:, :2]))
        near_edge = abs(abs(world[:, :2])-1) <= margin
        within_other_axis = abs(world[:, :2]) <= 1+margin
        uncertain.append((near_edge[:, 0] & within_other_axis[:, 1]) |
                         (near_edge[:, 1] & within_other_axis[:, 0]))
    return np.asarray(uncertain)


def analysis_pixels(meta):
    corners = np.array([[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]])
    ndc = project(corners, meta['viewProjection'])
    px, py = (ndc[:, 0]+1)*meta['width']/2, (1-ndc[:, 1])*meta['height']/2
    left, right, top, bottom = px.min(), px.max(), py.min(), py.max()
    x, y = np.meshgrid(np.arange(max(0, int(left)-4), min(meta['width'], int(right)+6)),
                       np.arange(max(0, int(top)-4), min(meta['height'], int(bottom)+6)))
    edge = ((abs(x-left) <= 4) | (abs(x-right) <= 4) | (abs(y-top) <= 4) | (abs(y-bottom) <= 4))
    control_x, control_y = np.meshgrid(np.arange(16, meta['width']-16, 16), np.arange(16, meta['height']-16, 16))
    ids = np.unique(np.concatenate((y[edge]*meta['width']+x[edge],
                                    control_y.ravel()*meta['width']+control_x.ravel())))
    return ids % meta['width'], ids // meta['width']


def partition_error(diffuse, specular, accumulation, hits, batch, uncertain=None):
    radiance = accumulation[:, :3]/batch
    background_fraction = 1-hits.mean(axis=0)
    signal = diffuse[:, :3]+specular[:, :3]
    if uncertain is None:
        expected_surface = radiance-background_fraction[:, None]*BACKGROUND
        error = abs(signal-expected_surface)
    else:
        min_miss = 1-(hits | uncertain).mean(axis=0)
        max_miss = 1-(hits & ~uncertain).mean(axis=0)
        low = radiance-max_miss[:, None]*BACKGROUND
        high = radiance-min_miss[:, None]*BACKGROUND
        error = np.maximum(np.maximum(low-signal, signal-high), 0)
    bound = .002*np.maximum(abs(radiance), 1)+1e-5
    return error, bound


def assess(buffers, batch):
    meta = buffers['Albedo'][0]
    for name, fmt in zip(RESOURCES, FORMATS):
        metadata, values = buffers[name]
        if metadata['resource'] != 'PathTracing.'+name or metadata['format'] != fmt or not np.isfinite(values).all():
            raise ValueError('Invalid resource, format or nonfinite data')
        for key in ('width', 'height', 'sampleStartIndex', 'randomSeed', 'viewProjection', 'previousViewProjection'):
            if metadata[key] != meta[key]:
                raise ValueError('Capture frame mismatch: '+key)
    xs, ys = analysis_pixels(meta)
    hits = sample_hits(meta, xs, ys, batch)
    uncertain = uncertain_sample_hits(meta, xs, ys, batch)
    first = hits[0]
    mixed = hits.any(axis=0) & ~hits.all(axis=0)
    later_hit_first_miss = mixed & ~first
    later_miss_first_hit = mixed & first
    if not first.any() or first.all():
        raise ValueError('No primary hit/miss coverage')
    if batch > 1 and (not later_hit_first_miss.any() or not later_miss_first_hit.any()):
        raise ValueError('Need both directions of mixed first-sample hit/miss coverage')
    normal, depth, motion, albedo, diffuse, specular, accumulation = (
        buffers[name][1][ys, xs] for name in RESOURCES)
    color, roughness, _ = srgb_texture_material([.25, .5, .75], .37)
    strict_error, bound = partition_error(diffuse, specular, accumulation, hits, batch)
    error, bound = partition_error(diffuse, specular, accumulation, hits, batch, uncertain)
    all_miss = ~(hits | uncertain).any(axis=0)
    if int(all_miss.sum()) < 1000:
        raise ValueError('Insufficient all-miss background controls')
    checks = dict(
        firstSampleValidity=bool(np.array_equal(albedo[:, 3], first.astype(float))),
        firstSampleNormal=bool((abs(normal[first, :3]-[0, 0, -1]) <= .001).all()),
        firstSampleRoughness=bool((abs(normal[first, 3]-roughness) <= .001).all()),
        firstSampleAlbedo=bool((abs(albedo[first, :3]-color) <= .0015).all()),
        missPrimaryGuides=bool((normal[~first] == [0, 0, 0, 1]).all() and
            (depth[~first] == 0).all() and (motion[~first] == 0).all() and (albedo[~first] == 0).all()),
        viewDepth=bool((abs(depth[first, 0]-5) <= .0001).all()),
        hitTAgreement=bool(np.array_equal(diffuse[:, 3], specular[:, 3])),
        firstMissHitTZero=bool((diffuse[~first, 3] == 0).all()),
        firstHitTPositive=bool((diffuse[first, 3] > 0).all()),
        allMissSignalsZero=bool((diffuse[all_miss] == 0).all() and (specular[all_miss] == 0).all()),
        backgroundRadiance=bool((abs(accumulation[all_miss, :3]/batch-BACKGROUND) <= 1e-6).all()),
        sampleCount=bool((accumulation[:, 3] == batch).all()),
        nonzeroDiffuse=bool((diffuse[first, :3] > 0).any()),
        nonzeroSpecular=bool((specular[first, :3] > 0).any()),
        nonnegativeSignals=bool((diffuse[:, :3] >= 0).all() and (specular[:, :3] >= 0).all()),
        partitionIncludesAllMissSamples=bool((error <= bound).all()))
    if batch > 1:
        checks['laterSurfaceSignalsSurviveFirstMiss'] = bool(
            ((diffuse[later_hit_first_miss, :3]+specular[later_hit_first_miss, :3]) > 0).any())
    return dict(passed=all(checks.values()), checks=checks, samplesPerFrame=batch,
        sampleStartIndex=meta['sampleStartIndex'], pixels=len(xs), firstHitPixels=int(first.sum()),
        firstMissPixels=int((~first).sum()), mixedPixels=int(mixed.sum()),
        firstMissLaterHitPixels=int(later_hit_first_miss.sum()), firstHitLaterMissPixels=int(later_miss_first_hit.sum()),
        signalPartitionMaxError=float(error.max()),
        strictSignalPartitionMaxError=float(strict_error.max()),
        strictSignalPartitionPassed=bool((strict_error <= bound).all()),
        ambiguousSamples=int(uncertain.sum()), ambiguousPixels=int(uncertain.any(axis=0).sum()),
        hitClassificationUncertainty='8 * float32 epsilon * max(1, abs(world coordinate)); interval comparison, no pixels removed',
        signalPartitionBound='0.002 * max(abs(frame radiance), 1) + 1e-5')


def reanalyze(output, exe):
    report_path = output/'report.json'
    report = json.loads(report_path.read_text())
    if report['executableSha256'] != sha(exe):
        raise ValueError('Executable does not match original capture cohort')
    if not (output/'report-before-final-analysis.json').exists():
        write_json(output/'report-before-final-analysis.json', report)
    reference = None
    report['failures'] = []
    if [record['samplesPerFrame'] for record in report['records']] != report.get('batches', [1, 2, 4]):
        raise ValueError('Expected complete declared batch cohort')
    for record in report['records']:
        batch = record['samplesPerFrame']
        directory = output/str(batch)
        try:
            if len(record['captures']) != len(RESOURCES):
                raise ValueError('Incomplete captures')
            buffers = {}
            for capture, resource in zip(record['captures'], RESOURCES):
                path, log = directory/(resource+'.ptbuf'), directory/(resource+'.log')
                if capture['resource'] != resource or capture['sha256'] != sha(path) or capture['logSha256'] != sha(log):
                    raise ValueError('Capture hash mismatch')
                text = log.read_text(encoding='utf-8-sig')
                if '[ERROR]' in text or '[CORRUPTION]' in text:
                    raise ValueError('D3D12 errors')
                buffers[resource] = read_buffer(path)
            record['assessment'] = assess(buffers, batch)
            if not record['assessment']['passed']:
                raise ValueError('Hit/miss guide checks failed')
            if reference is None:
                reference = {name: buffers[name] for name in RESOURCES[:4]}
            else:
                compare_primary_guides(reference, buffers)
            record.pop('failure', None)
            record['firstSampleGuidesIdentical'] = True
        except Exception as error:
            record['failure'] = str(error)
            report['failures'].append(dict(samplesPerFrame=batch, error=str(error)))
    report['analysisScriptSha256'] = sha(Path(__file__).resolve())
    report['analysisUtc'] = datetime.now(timezone.utc).isoformat()
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(report_path, report)
    print(json.dumps([record.get('assessment', record.get('failure')) for record in report['records']]), flush=True)
    return bool(report['failures'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--analyze-only', action='store_true')
    parser.add_argument('--batches', type=int, choices=(1, 2, 4), nargs='+', default=[1, 2, 4])
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if not 0 <= args.seed <= 0xffffffff or args.batches != sorted(set(args.batches)):
        parser.error('Use uint32 seed and ascending unique batches')
    if args.analyze_only:
        return reanalyze(output, exe)
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    output.mkdir(parents=True, exist_ok=True)
    fixture = ROOT/'Assets/Scenes/PathTracingValidation/input-boundary'
    report = dict(schemaVersion=1, status='running', executableSha256=sha(exe),
        generatedUtc=datetime.now(timezone.utc).isoformat(),
        seed=args.seed, batches=args.batches,
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        sourceSha256={p: sha(ROOT/p) for p in ['Shaders/shaders_PathTracing.hlsl',
            'Tests/PathTracing/validate_hit_miss_guides.py', 'Tests/PathTracing/make_boundary_fixture.py']},
        sceneSha256=sha(fixture/'scene.json'), background=BACKGROUND, records=[], failures=[])
    reference = None
    for batch in args.batches:
        directory = output/str(batch)
        preset = json.loads((fixture/'render-preset.json').read_text())
        preset['pathTracing']['samplesPerFrame'] = batch
        write_json(directory/'preset.json', preset)
        record = dict(samplesPerFrame=batch, presetSha256=sha(directory/'preset.json'), captures=[])
        report['records'].append(record)
        buffers = {}
        try:
            for resource in RESOURCES:
                path, log = directory/(resource+'.ptbuf'), directory/(resource+'.log')
                command = [str(exe), '-SceneFile', str(fixture/'scene.json'), '-RenderPreset', str(directory/'preset.json'),
                    '-EnablePathTracing', '-PathTracingSeed', str(args.seed), '-DebugPreviewResource', 'PathTracing.'+resource,
                    '-CapturePath', str(path), '-CaptureAfterFrames', str(60//batch), '-LogToFile', str(log), '-ExitAfterCapture']
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
                try:
                    if child.wait(timeout=180):
                        raise RuntimeError('App failed')
                finally:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
                text = log.read_text(encoding='utf-8-sig')
                if '[ERROR]' in text or '[CORRUPTION]' in text:
                    raise ValueError('D3D12 errors')
                buffers[resource] = read_buffer(path)
                record['captures'].append(dict(resource=resource, command=command, sha256=sha(path),
                    logSha256=sha(log), metadata=buffers[resource][0], d3d12Errors=0))
                print('Captured', batch, resource, flush=True)
                write_json(output/'report.json', report)
            record['assessment'] = assess(buffers, batch)
            if not record['assessment']['passed']:
                raise ValueError('Hit/miss guide checks failed')
            if reference is None:
                reference = {name: buffers[name] for name in RESOURCES[:4]}
            else:
                compare_primary_guides(reference, buffers)
            record['firstSampleGuidesIdentical'] = True
            print(json.dumps(record['assessment']), flush=True)
        except Exception as error:
            record['failure'] = str(error)
            report['failures'].append(dict(samplesPerFrame=batch, error=str(error)))
        write_json(output/'report.json', report)
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
