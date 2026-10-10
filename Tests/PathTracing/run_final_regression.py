"""Run a bounded final PT regression with one explicit executable and serial GPU tasks."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
from validate_inputs import ROOT
from validate_part1 import sha, write_json
from validate_history import CASES as HISTORY_CASES, CHANGES, validate_records


PRIMARY_CASES = (
    ('input-plane-NormalRoughness-static', 'NormalRoughness', False),
    ('input-shifted-ViewZ-static', 'ViewZ', False),
    ('input-marker-Albedo-static', 'Albedo', False),
    ('input-shifted-MotionVectors-moving', 'MotionVectors', True),
)
MOTION_CASES = (('static', 0), ('positive-x', 0.05), ('negative-x', -0.05))
HISTORY_PAIRS = (
    ('baseline-16', 'paused-16'), ('baseline-16', 'reset-repeat-16'),
    ('resumed-32', 'fresh-32'), ('non-accumulated-index-12', 'non-accumulated-repeat-12'),
)


def finite_nonnegative(value):
    return type(value) in (int, float) and math.isfinite(value) and value >= 0


def validate_history_report(report):
    records = report.get('records', [])
    dimensions = validate_records(records, [record.get('case') for record in records])
    if report.get('dimensions') != list(dimensions) or report.get('errorCount') != 0:
        raise ValueError('Incomplete history timeline')
    for record in records:
        if (not isinstance(record.get('path'), str) or not record['path'] or
                not isinstance(record.get('sha256'), str) or
                not re.fullmatch(r'[0-9a-f]{64}', record['sha256']) or
                not finite_nonnegative(record.get('rawRgbaMaxAbs')) or
                record.get('gpuSampleCount') != record['accumulatedSamples']):
            raise ValueError('Missing history capture evidence')
    assessment = report.get('assessment', {})
    comparisons = assessment.get('exactComparisons', [])
    if ([(item.get('left'), item.get('right')) for item in comparisons] != list(HISTORY_PAIRS) or
            any(item.get('exact') is not True for item in comparisons)):
        raise ValueError('Missing or failed history replay assessment')
    changes = assessment.get('changedStateComparisons', [])
    if ([item.get('change') for item in changes] != list(CHANGES) or
            any(item.get('exact') is not True for item in changes)):
        raise ValueError('Missing or failed changed-state assessment')
    for item in changes:
        signal = item.get('mutationRmse')
        if item['change'] == 'resize':
            if 'mutationRmse' not in item or signal is not None:
                raise ValueError('Invalid resize assessment')
        elif not finite_nonnegative(signal) or signal <= 1e-5:
            raise ValueError('Missing observable history mutation')
    if not finite_nonnegative(assessment.get('resetMaxAbs')) or assessment['resetMaxAbs'] != 0:
        raise ValueError('Missing or failed reset assessment')
    fresh = next(record for record in records if record['case'] == 'fresh-32')
    max_limit = 2e-6 * max(1.0, fresh['rawRgbaMaxAbs'] / fresh['accumulatedSamples'])
    for metric, limit, ceiling in (('batchMaxAbsError', 'batchMaxAbsLimit', max_limit),
                                   ('batchRelativeRmse', 'batchRelativeRmseLimit', 1e-6)):
        value, threshold = assessment.get(metric), assessment.get(limit)
        if (not finite_nonnegative(value) or not finite_nonnegative(threshold) or
                threshold <= 0 or threshold > ceiling or value > threshold):
            raise ValueError('Missing or failed history batching assessment')
    return len(HISTORY_CASES)


def verify_history_capture_files(report):
    for record in report['records']:
        path = Path(record['path'])
        if not path.is_file() or sha(path) != record['sha256']:
            raise ValueError('Missing or changed history capture: '+record['case'])


def validate_child(name, report, executable_hash):
    expected_status = {'primary': 'done', 'motion': 'done', 'history': 'passed', 'emissive': 'passed'}[name]
    if report.get('status') != expected_status or report.get('failures') or report.get('failure') or report.get('error'):
        raise ValueError('Child report failed or incomplete: '+name)
    if report.get('executableSha256') != executable_hash:
        raise ValueError('Child executable mismatch: '+name)
    if name in ('primary', 'motion'):
        records = report.get('records', [])
        expected_count = len(PRIMARY_CASES) if name == 'primary' else len(MOTION_CASES)
        if len(records) != expected_count or any(record.get('failure') for record in records):
            raise ValueError('Incomplete native guide captures: '+name)
        for index, record in enumerate(records):
            result = record.get('result', {})
            if name == 'primary':
                case, resource, moving = PRIMARY_CASES[index]
                if (record.get('name') != case or result.get('resource') != resource or
                        record.get('moving') is not moving or
                        record.get('metadata', {}).get('resource') != 'PathTracing.'+resource):
                    raise ValueError('Unexpected primary guide case/resource')
            else:
                case, delta = MOTION_CASES[index]
                if (record.get('name') != case or record.get('deltaPerFrame') != delta or
                        record.get('metadata', {}).get('resource') != 'PathTracing.MotionVectors'):
                    raise ValueError('Unexpected object motion case/resource')
            key = 'halfPrecisionBoundPassed' if name == 'primary' and result.get('resource') == 'MotionVectors' else 'passed'
            if result.get(key) is not True or record.get('d3d12Errors') != 0:
                raise ValueError('Invalid native guide result: '+name)
        return len(records)
    if name == 'history':
        return validate_history_report(report)
    cases = report.get('cases', [])
    if [case.get('case') for case in cases] != ['baseline', 'backface', 'beyond', 'empty', 'shadow-off']:
        raise ValueError('Incomplete emissive controls')
    if any(case.get('status') != 'passed' or len(case.get('records', [])) != 3 or
           len(case.get('checks', [])) != 3 or not all(check.get('passed') is True for check in case['checks']) for case in cases):
        raise ValueError('Failed emissive controls')
    return 15


def test_counts(name, log):
    if name == 'python':
        match = re.search(r'Ran (\d+) tests', log)
        if not match or int(match[1]) == 0 or not re.search(r'^OK$', log, re.MULTILINE):
            raise ValueError('Python tests did not complete')
        return int(match[1])
    match = re.search(r'(\d+) tests failed out of (\d+)', log)
    if not match or int(match[1]) or int(match[2]) == 0:
        raise ValueError('CTest suite failed or empty')
    return int(match[2])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path, default=ROOT/'build/Debug/RtPbrSurvey.exe')
    parser.add_argument('--ctest', default=shutil.which('ctest') or 'ctest')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error('Use an empty output directory')
    output.mkdir(parents=True, exist_ok=True)
    script_root = ROOT/'Tests/PathTracing'
    def gpu(script, name, options=()):
        return [sys.executable, '-B', str(script_root/script), '--exe', str(exe), '--output', str(output/name), *options]
    tasks = [
        ('python', [sys.executable, '-B', '-m', 'unittest', 'discover', '-s', str(script_root), '-p', 'test_*.py']),
        ('ctest', [args.ctest, '--test-dir', str(ROOT/'build'), '-C', 'Debug', '--output-on-failure', '--no-tests=error']),
        ('primary', gpu('validate_inputs.py', 'primary', ['--cases',
            'input-plane-NormalRoughness-static,input-shifted-ViewZ-static,input-marker-Albedo-static,input-shifted-MotionVectors-moving'])),
        ('motion', gpu('validate_object_motion.py', 'motion')),
        ('history', gpu('validate_history.py', 'history')),
        ('emissive', gpu('validate_emissive_controls.py', 'emissive', ['--samples', '64', '--seed', '11'])),
    ]
    report = dict(schemaVersion=1, status='running', generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(['git', 'branch', '--show-current'], cwd=ROOT, text=True).strip(),
        dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip()),
        executable=str(exe), executableSha256=sha(exe),
        gpuDriver=subprocess.check_output(['nvidia-smi', '--query-gpu=name,driver_version', '--format=csv,noheader'], text=True).strip(),
        sourceSha256={str(path.relative_to(ROOT)): sha(path) for path in sorted(script_root.glob('*.py'))},
        scope='Bounded regression, not a complete convergence/performance/cross-GPU rerun', runs=[], failures=[])
    for name, command in tasks:
        print('Starting '+name, flush=True)
        log = output/(name+'-runner.log')
        run = dict(name=name, command=command, log=str(log), status='running')
        report['runs'].append(run)
        write_json(output/'report.json', report)
        try:
            with log.open('w', encoding='utf-8') as stream:
                result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
            run['exitCode'] = result.returncode
            run['logSha256'] = sha(log)
            if result.returncode:
                raise ValueError('Runner exited with code '+str(result.returncode))
            if name in ('python', 'ctest'):
                run['testsPassed'] = test_counts(name, log.read_text(encoding='utf-8'))
            else:
                child = output/name/'report.json'
                child_report = json.loads(child.read_text())
                run['captureCount'] = validate_child(name, child_report, report['executableSha256'])
                if name == 'history':
                    verify_history_capture_files(child_report)
                run['reportSha256'] = sha(child)
            run['status'] = 'passed'
        except Exception as error:
            run.update(status='failed', error=str(error))
            report['failures'].append(dict(name=name, error=str(error)))
        write_json(output/'report.json', report)
        print('Finished '+name+': '+run['status'], flush=True)
    report['status'] = 'passed' if not report['failures'] else 'failed'
    report['captureCount'] = sum(run.get('captureCount', 0) for run in report['runs'])
    report['completedUtc'] = datetime.now(timezone.utc).isoformat()
    write_json(output/'report.json', report)
    return bool(report['failures'])


if __name__ == '__main__':
    raise SystemExit(main())
