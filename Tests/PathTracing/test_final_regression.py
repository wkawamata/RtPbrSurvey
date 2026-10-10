import unittest
from run_final_regression import validate_child, test_counts


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
        records = [dict(result=dict(resource='ViewZ', passed=True), d3d12Errors=0) for _ in range(4)]
        report = dict(status='done', executableSha256='hash', records=records)
        self.assertEqual(validate_child('primary', report, 'hash'), 4)
        records[0]['result']['passed'] = False
        with self.assertRaises(ValueError):
            validate_child('primary', report, 'hash')

    def test_no_tests_is_not_success(self):
        for name, log in (('python', 'Ran 0 tests\nOK\n'), ('ctest', '0 tests failed out of 0')):
            with self.assertRaises(ValueError):
                test_counts(name, log)
        self.assertEqual(test_counts('python', 'Ran 150 tests in 1s\n\nOK\n'), 150)
        self.assertEqual(test_counts('ctest', '100% tests passed, 0 tests failed out of 26'), 26)


if __name__ == '__main__':
    unittest.main()
