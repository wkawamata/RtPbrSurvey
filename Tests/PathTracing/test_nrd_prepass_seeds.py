import copy
import unittest

import test_nrd_glossy_seeds as fixtures
from validate_nrd_prepass_seeds import compare_rows


class PrepassSeedTests(unittest.TestCase):
    def test_all_seed_frame_roi_comparisons_are_retained(self):
        rows = fixtures.GlossySeedTests().rows()
        result = compare_rows(rows, copy.deepcopy(rows))
        self.assertEqual(len(result), 140)
        self.assertTrue(all(row['meanRgbBiasDelta'] == 0 and row['nrdNrmseDelta'] == 0 and
            not row['regression'] and not row['recovered'] for row in result))

    def test_incomplete_duplicate_or_different_reference_is_rejected(self):
        rows = fixtures.GlossySeedTests().rows()
        reference = copy.deepcopy(rows)
        for row in reference:
            row['referencePairNrmse'] += .001
        for changed in (rows[:-1], rows[:-1]+[rows[0]], reference):
            with self.assertRaises(ValueError):
                compare_rows(rows, changed)


if __name__ == '__main__':
    unittest.main()
