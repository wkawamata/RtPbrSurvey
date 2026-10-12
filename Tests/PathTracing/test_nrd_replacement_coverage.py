import unittest

import numpy as np

from audit_nrd_replacement_coverage import coverage, witnesses


class ReplacementCoverageTests(unittest.TestCase):
    def test_five_by_five_covers_distance_two_not_three(self):
        seeds = np.zeros((7, 7), dtype=bool)
        seeds[3, 1] = True
        labels = np.zeros((7, 7), dtype=int)
        found = witnesses(seeds, labels, 2)
        self.assertTrue(found[3, 3])
        self.assertFalse(found[3, 4])

    def test_no_wraparound_at_screen_edges(self):
        seeds = np.zeros((7, 7), dtype=bool)
        seeds[0, 0] = True
        found = witnesses(seeds, np.zeros((7, 7), dtype=int), 2)
        self.assertFalse(found[-1, -1])

    def test_other_surface_and_unknown_labels_cannot_supply_witness(self):
        seeds = np.zeros((5, 5), dtype=bool)
        seeds[2, 2] = True
        labels = np.zeros((5, 5), dtype=int)
        labels[2, 2] = 1
        labels[2, 3] = -1
        found = witnesses(seeds, labels, 2)
        self.assertFalse(found[2, 1])
        self.assertFalse(found[2, 3])

    def test_holes_are_reported_not_fabricated(self):
        missing = np.ones((5, 5), dtype=bool)
        result = coverage(missing, np.zeros((5, 5), dtype=bool), np.zeros((5, 5), dtype=int), 2)
        self.assertEqual(result['holes'], 25)
        self.assertEqual(result['witnessed'], 0)

    def test_tiny_image_has_no_wrapped_witness(self):
        missing = np.ones((1, 1), dtype=bool)
        result = coverage(missing, np.zeros((1, 1), dtype=bool), np.zeros((1, 1), dtype=int), 2)
        self.assertEqual(result['holes'], 1)

    def test_overlapping_and_invalid_inputs_are_rejected(self):
        values = np.ones((5, 5), dtype=bool)
        with self.assertRaises(ValueError):
            coverage(values, values, np.zeros((5, 5), dtype=int), 2)
        with self.assertRaises(ValueError):
            witnesses(values, np.zeros((5, 5), dtype=int), 3)
