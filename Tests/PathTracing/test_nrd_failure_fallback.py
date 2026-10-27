import copy
import unittest

from validate_nrd_failure_fallback import assess_failure, FALLBACK


class FailureFallbackTests(unittest.TestCase):
    def record(self):
        return dict(nrdPreviewRequested=True, nrdPresentation=FALLBACK, nrdFailureCount=1,
            nrdFailure='preparation: invalid final dispatch', nrdPreparedDispatches=34, nrdPlannedDispatches=35)

    def test_late_failure_requires_complete_earlier_preparation(self):
        self.assertEqual(assess_failure(self.record(), 'last-dispatch')['preparedDispatches'], 34)
        for key, value in (('nrdPreparedDispatches', 0), ('nrdPlannedDispatches', 37),
                ('nrdPreparedDispatches', True), ('nrdFailureCount', 2), ('nrdFailureCount', True),
                ('nrdFailure', ''), ('nrdFailure', 'initialization: missing')):
            record = self.record()
            record[key] = value
            with self.assertRaises(ValueError):
                assess_failure(record, 'last-dispatch')

    def test_missing_or_stale_success_is_rejected(self):
        for key in self.record():
            record = self.record()
            del record[key]
            with self.assertRaises(ValueError):
                assess_failure(record, 'last-dispatch')
        record = self.record()
        record['nrdPresentation'] = 'NRD RELAX diagnostic (per-pixel Native fallback)'
        with self.assertRaises(ValueError):
            assess_failure(record, 'last-dispatch')

    def test_initialization_has_no_dispatches(self):
        record = dict(nrdPreviewRequested=True, nrdPresentation=FALLBACK, nrdFailureCount=1,
            nrdFailure='initialization: missing manifest', nrdPreparedDispatches=0, nrdPlannedDispatches=0)
        assess_failure(record, 'initialization')
        changed = copy.deepcopy(record)
        changed['nrdPlannedDispatches'] = 1
        with self.assertRaises(ValueError):
            assess_failure(changed, 'initialization')


if __name__ == '__main__':
    unittest.main()
