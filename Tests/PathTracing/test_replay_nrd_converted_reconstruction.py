import copy
import unittest

import numpy as np

from replay_nrd_converted_reconstruction import validate_report, assess_chain


class ConvertedReconstructionTests(unittest.TestCase):
    def report(self):
        return dict(status='passed', failures=[], sourceReportSha256='source', inputReady=False,
            runtimeGeometrySnapshotVerified=False, records=[
                dict(variant='hit-only', hypotheticalEnvironment=False, d3d12Errors=0),
                dict(variant='hypothetical-environment', hypotheticalEnvironment=True, d3d12Errors=0)])

    def test_exact_provenance(self):
        self.assertEqual(len(validate_report(self.report(), 'source')), 2)
        for mutate in (lambda r: r['records'].pop(),
                       lambda r: r['records'].__setitem__(1, copy.deepcopy(r['records'][0])),
                       lambda r: r.__setitem__('sourceReportSha256', 'stale'),
                       lambda r: r['records'][0].__setitem__('d3d12Errors', 1)):
            report = self.report()
            mutate(report)
            with self.assertRaises(ValueError):
                validate_report(report, 'source')

    def fixture(self):
        packed = np.array([[[2, 4, 6, 2], [0, 0, 0, 0]]], dtype=np.float32)
        factors = np.full((1, 2, 3), .5)
        physical = packed[..., :3]*factors
        states = np.array([[[2, 1, 2, 0], [0, 0, 0, 0]]])
        primary = np.array([[True, False]])
        return packed, factors, physical, states, primary, np.array([[0, -1]]), np.array([[[2], [1e6]]])

    def test_rgb_and_exclusion_preserved(self):
        args = self.fixture()
        self.assertEqual(assess_chain(args[0].copy(), *args)['maximumPhysicalRoundTripError'], 0)
        for pixel, channel, value in ((0, 0, 3), (1, 0, 1), (0, 3, np.nan), (0, 3, -1)):
            values = args[0].copy()
            values[0, pixel, channel] = value
            with self.assertRaises(ValueError):
                assess_chain(values, *args)

    def test_wrong_factor_rejected(self):
        args = list(self.fixture())
        args[1] = args[1]*2
        with self.assertRaises(ValueError):
            assess_chain(args[0].copy(), *args)
