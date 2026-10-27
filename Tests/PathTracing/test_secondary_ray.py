import unittest

import numpy as np

from validate_secondary_ray import assess


class SecondaryRayTests(unittest.TestCase):
    def fixture(self):
        meta = dict(resource='PathTracing.SecondaryRayData', format=2, width=3, height=1,
                    randomSeed=7, sampleStartIndex=30, viewProjection=[1], previousViewProjection=[1])
        albedo_meta = dict(meta, resource='PathTracing.Albedo', format=10)
        data = np.array([[[3, 1, 1, 0], [100, 2, 2, 0], [0, 0, 0, 0]]], dtype=float)
        albedo = np.array([[[.5, .5, .5, 1], [.5, .5, .5, 1], [0, 0, 0, 0]]], dtype=float)
        return (meta, data), (albedo_meta, albedo)

    def test_hit_miss_and_proposals(self):
        self.assertTrue(assess(*self.fixture(), 'indirect')['passed'])

    def test_unknown_mode_rejected(self):
        with self.assertRaises(ValueError):
            assess(*self.fixture(), 'unknown')

    def test_invalid_maximum_distance_rejected(self):
        for value in (0, -1, np.inf, np.nan):
            with self.assertRaises(ValueError):
                assess(*self.fixture(), 'indirect', ray_max=value)

    def test_untraced_is_not_environment_miss(self):
        data, albedo = self.fixture()
        data[1][:] = 0
        self.assertTrue(assess(data, albedo, 'one-segment')['passed'])
        self.assertFalse(assess(data, albedo, 'metal')['passed'])

    def test_metal_proposal_is_checked(self):
        self.assertFalse(assess(*self.fixture(), 'metal')['passed'])
        data, albedo = self.fixture()
        data[1][0, 0, 1] = 2
        self.assertTrue(assess(data, albedo, 'metal')['passed'])

    def test_bad_distance_state_and_primary_miss(self):
        for index, channel, value, check in [(0, 0, -1, 'hitDistance'), (1, 0, 99, 'missDistance'),
                                            (0, 2, 3, 'status'), (2, 0, 1, 'primaryMiss')]:
            data, albedo = self.fixture()
            data[1][0, index, channel] = value
            self.assertFalse(assess(data, albedo, 'indirect')['checks'][check])

    def test_wrong_frame_and_nonfinite_rejected(self):
        data, albedo = self.fixture()
        albedo[0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            assess(data, albedo, 'indirect')
        data, albedo = self.fixture()
        albedo[1][0, 0, 3] = .5
        with self.assertRaises(ValueError):
            assess(data, albedo, 'indirect')
        data, albedo = self.fixture()
        data[1][0, 0, 0] = np.inf
        with self.assertRaises(ValueError):
            assess(data, albedo, 'indirect')


if __name__ == '__main__':
    unittest.main()
