import unittest

import numpy as np

from pilot_nrd_fast_reference import identical, speed_result, ORDER


class FastReferenceTests(unittest.TestCase):
    def test_equivalence_is_bit_exact_not_a_relaxed_quality_threshold(self):
        a = np.ones((2, 3, 4), dtype=np.float64)
        self.assertTrue(identical(a, a.copy())['bitIdentical'])
        b = a.copy()
        b[0, 0, 0] += 1e-10
        with self.assertRaises(ValueError):
            identical(a, b)
        a[0, 0, 0] = 0.0
        b = a.copy()
        b[0, 0, 0] = -0.0
        with self.assertRaises(ValueError):
            identical(a, b)

    def test_nonfinite_or_wrong_sized_image_is_rejected(self):
        a = np.ones((2, 3, 4))
        for b in (a[:1], a*np.nan):
            with self.assertRaises(ValueError):
                identical(a, b)

    def rows(self, off=2):
        return [dict(order=i, mode=mode, elapsedSeconds=4 if mode == 'validation-on' else off)
            for i, mode in enumerate(ORDER)]

    def test_speed_target_is_predeclared_and_includes_all_timed_runs(self):
        self.assertEqual(speed_result(self.rows())['speedup'], 2)
        self.assertTrue(speed_result(self.rows())['materiallyFaster'])
        self.assertFalse(speed_result(self.rows(off=3))['materiallyFaster'])

    def test_missing_duplicate_or_invalid_timings_are_rejected(self):
        rows = self.rows()
        for changed in (rows[:-1], rows[:-1]+[rows[0]], [dict(r, elapsedSeconds=0) for r in rows],
                [dict(r, elapsedSeconds=float('nan')) for r in rows],
                [dict(order=r['order'], mode=r['mode']) for r in rows]):
            with self.assertRaises(ValueError):
                speed_result(changed)


if __name__ == '__main__':
    unittest.main()
