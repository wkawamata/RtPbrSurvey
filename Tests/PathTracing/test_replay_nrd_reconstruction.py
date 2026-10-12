import unittest
import tempfile
import json
from pathlib import Path

import numpy as np

from replay_nrd_reconstruction import distance_input, assess_output, load_cohort
from validate_part1 import sha


class ReconstructionReplayTests(unittest.TestCase):
    def test_duplicate_capture_records_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder/'scene.json').write_text('{}')
            record = dict(resource='DenoiserPrimaryPosition', d3d12Errors=0, sha256='unused')
            report = dict(status='passed', assessment=dict(passed=True), nativeRadianceBitIdentical=True,
                sceneSha256=sha(folder/'scene.json'), records=[record, record])
            (folder/'report.json').write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                load_cohort(folder)

    def test_environment_encoding_is_explicitly_hypothetical(self):
        states = np.array([[[2., 1., 2., 0.], [0., 2., 100., 0.], [0., 3., 0., 0.]]])
        primary = np.ones((1, 3), dtype=bool)
        strict = distance_input(states, primary, 100, False)
        hypothetical = distance_input(states, primary, 100, True)
        np.testing.assert_array_equal(strict[..., 3], [[2, 0, 0]])
        np.testing.assert_array_equal(hypothetical[..., 3], [[2, 65504, 0]])

    def test_bad_states_and_unproven_miss_values_are_rejected(self):
        primary = np.ones((1, 1), dtype=bool)
        for values in ([0, 0, 0, 0], [0, 2, 99, 0], [0, 3, 1, 0], [2, 1, 2, 1], [0, 8, 0, 0],
                [np.nan, 1, 2, 0]):
            with self.assertRaises(ValueError):
                distance_input(np.array([[values]], dtype=float), primary, 100, False)

    def test_unresolved_edge_is_reported_not_accepted(self):
        states = np.array([[[0., 3., 0., 0.]]])
        primary = np.ones((1, 1), dtype=bool)
        values = distance_input(states, primary, 100, False)
        result = assess_output(values.copy(), values, states, primary, np.full((1, 1), -1),
            np.array([[[10.]]]))
        self.assertEqual(result['unresolvedUnlabelled'], 1)
        self.assertEqual(result['allActiveUnresolved'], 1)

    def test_rgb_change_and_nonfinite_output_are_rejected(self):
        states = np.array([[[0., 3., 0., 0.]]])
        primary = np.ones((1, 1), dtype=bool)
        values = distance_input(states, primary, 100, False)
        for changed in (0., np.nan):
            output = values.copy()
            output[..., 0] = changed
            with self.assertRaises(ValueError):
                assess_output(output, values, states, primary, np.zeros((1, 1)), np.array([[[10.]]]))
