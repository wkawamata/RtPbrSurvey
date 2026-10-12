import unittest

import numpy as np

from audit_nrd_replacement_coverage import coverage, witnesses, assess_plane_normals


class ReplacementCoverageTests(unittest.TestCase):
    def test_plane_normal_assessment_rejects_wrong_guides(self):
        guide = np.array([[[0., 1., 0., .5]]])
        labels = np.zeros((1, 1), dtype=int)
        planes = [(None, None, None, np.array([0, 1, 0]), None, None)]
        self.assertEqual(assess_plane_normals(guide, labels, planes)['checkedPixels'], 1)
        guide[..., 1] = -1
        with self.assertRaises(ValueError):
            assess_plane_normals(guide, labels, planes)
        with self.assertRaises(ValueError):
            assess_plane_normals(guide, np.full((1, 1), -1), planes)

    def test_normal_and_roughness_boundaries_reject_witnesses(self):
        seeds = np.zeros((3, 3), dtype=bool)
        seeds[1, 1] = True
        labels = np.zeros((3, 3), dtype=int)
        guide = np.zeros((3, 3, 4))
        guide[..., 2] = 1
        guide[..., 3] = .5
        self.assertTrue(witnesses(seeds, labels, 1, guide)[1, 2])
        guide[1, 2, :3] = [1, 0, 0]
        self.assertFalse(witnesses(seeds, labels, 1, guide)[1, 2])
        guide[1, 2] = [0, 0, 1, .7]
        self.assertFalse(witnesses(seeds, labels, 1, guide)[1, 2])
        guide[1, 2] = [0, 0, 0, .5]
        self.assertFalse(witnesses(seeds, labels, 1, guide)[1, 2])
        guide[1, 2] = [0, 0, 1, .5]
        labels[1, 2] = -2
        self.assertFalse(witnesses(seeds, labels, 1, guide)[1, 2])

    def test_invalid_normal_guide_is_rejected(self):
        seeds = np.zeros((1, 1), dtype=bool)
        labels = np.zeros((1, 1), dtype=int)
        for guide in (np.zeros((1, 1, 3)), np.full((1, 1, 4), np.nan),
                np.array([[[0, 0, 1, 2]]])):
            with self.assertRaises(ValueError):
                witnesses(seeds, labels, 1, guide)

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
