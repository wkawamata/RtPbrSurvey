"""Additional world-normal, seed-repeat, legacy PFM and preview controls for Part 5."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
from validate_inputs import ROOT, analyze, read_buffer
from validate_part1 import write_json, sha
from compare_hdr import read_pfm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT/'bin/PathTracingValidation/part5-inputs-final')
    parser.add_argument('--output', type=Path, default=ROOT/'bin/PathTracingValidation/part5-controls')
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--powershell', default='pwsh')
    args = parser.parse_args()
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    import numpy as np
    base = json.loads((args.input/'report.json').read_text(encoding='utf-8'))
    if base['failures'] or len(base['records']) != 20:
        raise ValueError('Need the completed final cohort')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = dict(schemaVersion=1, testedCommit=base['testedCommit'], executableSha256=base['executableSha256'],
        records=[], failures=[], checks={})
    records = {r['name']: r for r in base['records']}
    def run(name, reference, resource=None, seed=None, extension='.ptbuf'):
        source = records[reference]
        path, log = output/(name+extension), output/(name+'.log')
        command = list(source['command'])
        for flag, value in [('-CapturePath', str(path)), ('-LogToFile', str(log))]:
            command[command.index(flag)+1] = value
        if resource:
            command[command.index('-DebugPreviewResource')+1] = 'PathTracing.'+resource
        if seed is not None:
            command[command.index('-PathTracingSeed')+1] = str(seed)
        record = dict(name=name, command=command, sceneSha256=source['sceneSha256'],
            presetSha256=source['presetSha256'], path=str(path.relative_to(ROOT)))
        report['records'].append(record)
        try:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
            try:
                code = child.wait(timeout=180)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], check=False)
                raise
            if code:
                raise RuntimeError(f'App exit {code}')
            text = log.read_text(encoding='utf-8-sig')
            errors = [line for line in text.splitlines() if '[ERROR]' in line or '[CORRUPTION]' in line]
            if errors:
                raise RuntimeError(str(errors[:2]))
            record.update(sha256=sha(path), logSha256=sha(log), d3d12Errors=0)
            if extension == '.ptbuf':
                meta, result = analyze(path, 'marker' in reference)
                record.update(metadata=meta, result=result)
                return read_buffer(path)
            dimensions, data = read_pfm(path, [16, 16, 32, 32])
            record.update(dimensions=dimensions, roi=[16, 16, 32, 32], finiteRgbCount=len(data))
        except Exception as error:
            record['failure'] = str(error)
            report['failures'].append(dict(name=name, error=str(error)))
            raise
        finally:
            write_json(output/'report.json', report)
    original = ROOT/records['input-marker-Albedo-static']['path']
    _, original_values = read_buffer(original)
    _, repeated = run('same-seed-marker', 'input-marker-Albedo-static')
    _, different = run('different-seed-marker', 'input-marker-Albedo-static', seed=8)
    report['checks']['fixedSeedPayloadIdentical'] = bool(np.array_equal(original_values, repeated))
    report['checks']['differentSeedChangedPixels'] = int(np.any(original_values != different, axis=2).sum())
    meta, normal = run('moving-world-normal', 'input-shifted-MotionVectors-moving', resource='NormalRoughness')
    report['checks']['movingWorldNormalPassed'] = report['records'][-1]['result']['passed']
    forward = np.asarray(meta['cameraTarget'])-np.asarray(meta['cameraPosition'])
    forward /= np.linalg.norm(forward)
    right = np.cross([0, 1, 0], forward)
    right /= np.linalg.norm(right)
    report['checks']['viewSpaceAlternativeNormal'] = (np.array([0, 0, -1]) @ np.array([right, np.cross(forward, right), forward]).T).tolist()
    run('moving-shifted-depth', 'input-shifted-MotionVectors-moving', resource='ViewZ')
    report['checks']['movingDepthImplementationPassed'] = report['records'][-1]['result']['shaderDefinitionPassed']
    run('legacy-accumulation', 'input-plane-Albedo-static', extension='.pfm')
    command = [args.powershell, '-NoProfile', '-File', str(ROOT/'Tests/PathTracing/Invoke-MotionVectorValidation.ps1'),
        '-SceneFile', str(ROOT/'Assets/Scenes/PathTracingValidation/input-shifted/scene.json'),
        '-RenderPreset', str(ROOT/'Assets/Scenes/PathTracingValidation/input-shifted/render-preset.json'),
        '-OutputDirectory', str(output/'preview')]
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=380)
    (output/'preview-script.log').write_bytes(completed.stdout.encode('utf-8')+completed.stderr.encode('utf-8'))
    report['previewCommand'] = command
    if completed.returncode:
        report['failures'].append(dict(name='preview-script', error=completed.stderr))
    else:
        report['preview'] = json.loads((output/'preview/path-tracing-motion-vectors.json').read_text(encoding='utf-8-sig'))
    report['checks']['d3d12Errors'] = sum(r.get('d3d12Errors', 0) for r in report['records']) + report.get('preview', {}).get('result', {}).get('d3d12ErrorCount', 0)
    report['artifacts'] = [dict(path=str(p.relative_to(ROOT)), sha256=sha(p)) for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'report.json']
    report['status'] = 'done' if not report['failures'] else 'incomplete'
    write_json(output/'report.json', report)
    print(json.dumps(report['checks'], indent=2))
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
