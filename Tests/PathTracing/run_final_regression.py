"""Run a bounded final PT regression with one explicit executable and serial GPU tasks."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from validate_inputs import ROOT
from validate_part1 import sha, write_json
from validate_history import CASES as HISTORY_CASES


def validate_child(name, report, executable_hash):
    expected_status = {'primary': 'done', 'motion': 'done', 'history': 'passed', 'emissive': 'passed'}[name]
    if report.get('status') != expected_status or report.get('failures') or report.get('failure') or report.get('error'):
        raise ValueError('Child report failed or incomplete: '+name)
    if report.get('executableSha256') != executable_hash:
        raise ValueError('Child executable mismatch: '+name)
    if name in ('primary', 'motion'):
        records = report.get('records', [])
        expected_count = 4 if name == 'primary' else 3
        if len(records) != expected_count or any(record.get('failure') for record in records):
            raise ValueError('Incomplete native guide captures: '+name)
        for record in records:
            result = record.get('result', {})
            key = 'halfPrecisionBoundPassed' if name == 'primary' and result.get('resource') == 'MotionVectors' else 'passed'
            if result.get(key) is not True or record.get('d3d12Errors') != 0:
                raise ValueError('Invalid native guide result: '+name)
        return len(records)
    if name == 'history':
        if [record.get('case') for record in report.get('records', [])] != list(HISTORY_CASES) or report.get('errorCount') != 0:
            raise ValueError('Incomplete history timeline')
        return len(HISTORY_CASES)
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
                run['captureCount'] = validate_child(name, json.loads(child.read_text()), report['executableSha256'])
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
