import copy
from pathlib import Path
import tempfile
import unittest
from run_final_regression import (PRIMARY_CASES, MOTION_CASES, HISTORY_PAIRS,
                                  validate_child, test_counts, verify_history_capture_files)
from validate_history import CASES, CHANGES
from validate_part1 import sha


def guide_report(name):
    if name == 'primary':
        records = [dict(name=case, moving=moving, metadata=dict(resource='PathTracing.'+resource),
                        result=dict(resource=resource, passed=True, halfPrecisionBoundPassed=True), d3d12Errors=0)
                   for case, resource, moving in PRIMARY_CASES]
    else:
        records = [dict(name=case, deltaPerFrame=delta, metadata=dict(resource='PathTracing.MotionVectors'),
                        result=dict(passed=True), d3d12Errors=0) for case, delta in MOTION_CASES]
    return dict(status='done', executableSha256='hash', records=records)


def history_report():
    records = []
    reasons = dict(camera='Camera', light='Lighting', material='Material', geometry='Scene', resize='Render Size')
    for case, (count, index, valid, batch) in CASES.items():
        record = dict(case=case, accumulatedSamples=count, frameSampleIndex=index, historyValid=valid,
                      samplesPerFrame=batch, paused=True, randomSeed=11, renderWidth=2, renderHeight=2,
                      path=case+'.ptbuf', sha256='a'*64, rawRgbaMaxAbs=count, gpuSampleCount=count)
        if case.startswith('resize-'):
            record.update(renderWidth=1280, renderHeight=720)
        if case.endswith('-changed-16'):
            reason = reasons[case.split('-')[0]]
            record.update(resetReason=reason, entryResetReason='Pending Resize' if case.startswith('resize-') else reason)
        elif case.endswith('-fresh-16'):
            record['resetReason'] = 'Manual'
        records.append(record)
    assessment = dict(exactComparisons=[dict(left=left, right=right, exact=True) for left, right in HISTORY_PAIRS],
                      changedStateComparisons=[dict(change=change, exact=True, mutationRmse=None if change == 'resize' else 0.1)
                                               for change in CHANGES], resetMaxAbs=0,
                      batchMaxAbsError=1e-7, batchMaxAbsLimit=2e-6,
                      batchRelativeRmse=1e-7, batchRelativeRmseLimit=1e-6)
    return dict(status='passed', executableSha256='hash', records=records, dimensions=[2, 2],
                errorCount=0, assessment=assessment)


class FinalRegressionTests(unittest.TestCase):
    def test_empty_and_failed_child_rejected_despite_exit_zero(self):
        for report in ({}, dict(status='failed', executableSha256='hash'),
                       dict(status='done', executableSha256='hash', records=[])):
            with self.assertRaises(ValueError):
                validate_child('primary', report, 'hash')

    def test_hash_mismatch_rejected(self):
        with self.assertRaises(ValueError):
            validate_child('primary', dict(status='done', executableSha256='old'), 'new')

    def test_native_captures_require_numeric_pass(self):
        report = guide_report('primary')
        self.assertEqual(validate_child('primary', report, 'hash'), 4)
        report['records'][0]['result']['passed'] = False
        with self.assertRaises(ValueError):
            validate_child('primary', report, 'hash')

    def test_guide_duplicates_missing_and_wrong_cases_rejected(self):
        for name in ('primary', 'motion'):
            valid = guide_report(name)
            self.assertEqual(validate_child(name, valid, 'hash'), len(valid['records']))
            for mutation in ('duplicate', 'missing', 'case', 'resource', 'motion'):
                report = copy.deepcopy(valid)
                if mutation == 'duplicate':
                    report['records'] = [copy.deepcopy(report['records'][0]) for _ in report['records']]
                elif mutation == 'missing':
                    report['records'].pop()
                elif mutation == 'case':
                    report['records'][0]['name'] = 'other-case'
                elif mutation == 'resource':
                    report['records'][0]['metadata']['resource'] = 'PathTracing.ViewZ'
                elif name == 'primary':
                    report['records'][-1]['moving'] = False
                else:
                    report['records'][-1]['deltaPerFrame'] = 0.05
                with self.subTest(name=name, mutation=mutation), self.assertRaises(ValueError):
                    validate_child(name, report, 'hash')

    def test_primary_result_resource_mismatch_rejected(self):
        report = guide_report('primary')
        report['records'][0]['result']['resource'] = 'ViewZ'
        with self.assertRaises(ValueError):
            validate_child('primary', report, 'hash')

    def test_history_missing_assessment_and_metrics_rejected(self):
        valid = history_report()
        self.assertEqual(validate_child('history', valid, 'hash'), 19)
        for field in ('assessment', *valid['assessment']):
            report = copy.deepcopy(valid)
            del (report if field == 'assessment' else report['assessment'])[field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_child('history', report, 'hash')

    def test_history_failed_nonfinite_and_inflated_assessments_rejected(self):
        mutations = (
            ('resetMaxAbs', 0.1), ('batchMaxAbsError', 3e-6), ('batchRelativeRmse', 2e-6),
            ('batchMaxAbsLimit', 1.0), ('batchRelativeRmseLimit', 1.0),
            ('batchMaxAbsError', float('nan')), ('batchRelativeRmse', float('inf')),
        )
        for field, value in mutations:
            report = history_report()
            report['assessment'][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate_child('history', report, 'hash')
        for field in ('exactComparisons', 'changedStateComparisons'):
            for mutation in ('failed', 'duplicate', 'missing', 'signal'):
                report = history_report()
                items = report['assessment'][field]
                if mutation == 'failed':
                    items[0]['exact'] = False
                elif mutation == 'duplicate':
                    items[1] = copy.deepcopy(items[0])
                elif mutation == 'missing':
                    items.pop()
                elif field == 'changedStateComparisons':
                    items[0]['mutationRmse'] = 0
                else:
                    continue
                with self.subTest(field=field, mutation=mutation), self.assertRaises(ValueError):
                    validate_child('history', report, 'hash')

    def test_history_missing_runtime_state_and_capture_evidence_rejected(self):
        for field in ('accumulatedSamples', 'frameSampleIndex', 'historyValid', 'paused', 'randomSeed',
                      'samplesPerFrame', 'renderWidth', 'path', 'sha256', 'gpuSampleCount', 'rawRgbaMaxAbs'):
            report = history_report()
            del report['records'][0][field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_child('history', report, 'hash')
        for field, value in (('gpuSampleCount', 99), ('sha256', 'invalid'), ('rawRgbaMaxAbs', float('nan'))):
            report = history_report()
            report['records'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_child('history', report, 'hash')

    def test_history_capture_file_missing_or_changed_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'capture.ptbuf'
            path.write_bytes(b'capture evidence')
            report = dict(records=[dict(case='baseline-16', path=str(path), sha256=sha(path))])
            verify_history_capture_files(report)
            path.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                verify_history_capture_files(report)
            path.unlink()
            with self.assertRaises(ValueError):
                verify_history_capture_files(report)

    def test_no_tests_is_not_success(self):
        for name, log in (('python', 'Ran 0 tests\nOK\n'), ('ctest', '0 tests failed out of 0')):
            with self.assertRaises(ValueError):
                test_counts(name, log)
        self.assertEqual(test_counts('python', 'Ran 150 tests in 1s\n\nOK\n'), 150)
        self.assertEqual(test_counts('ctest', '100% tests passed, 0 tests failed out of 26'), 26)


if __name__ == '__main__':
    unittest.main()
