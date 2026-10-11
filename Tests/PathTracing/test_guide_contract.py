import unittest
import numpy as np
from validate_guide_contract import assess, compare_primary_guides, RESOURCES, FORMATS


class GuideContractTests(unittest.TestCase):
    def fixture(self):
        values = (
            [[[0, 0, -1, .5], [0, 0, 0, 1]]],
            [[[5], [0]]], [[[0, 0], [0, 0]]],
            [[[.5, .5, .5, 1], [0, 0, 0, 0]]],
            [[[.25, .25, .25, 5], [0, 0, 0, 0]]],
            [[[.125, .125, .125, 5], [0, 0, 0, 0]]],
            [[[.375, .375, .375, 1], [2, 2, 2, 1]]])
        return {name: (dict(resource='PathTracing.'+name, format=fmt, width=2,
                           height=1, randomSeed=7, sampleStartIndex=29,
                           viewProjection=[1], previousViewProjection=[1]),
                       np.array(value, dtype=float))
                for name, fmt, value in zip(RESOURCES, FORMATS, values)}

    def test_partition_excludes_primary_background(self):
        self.assertTrue(assess(self.fixture())['passed'])

    def test_wrong_frame_and_format_rejected(self):
        for key, value in [('sampleStartIndex', 30), ('format', 2)]:
            buffers = self.fixture()
            buffers['SpecularRadianceHitT'][0][key] = value
            with self.assertRaises(ValueError):
                assess(buffers)

    def test_miss_sentinel_and_hit_distance_checked(self):
        buffers = self.fixture()
        buffers['DiffuseRadianceHitT'][1][0, 1, 3] = 1
        self.assertFalse(assess(buffers)['passed'])

    def test_wrong_partition_and_accumulation_checked(self):
        buffers = self.fixture()
        buffers['Accumulation'][1][0, 0, 0] = 1
        self.assertFalse(assess(buffers)['checks']['signalPartition'])
        buffers = self.fixture()
        buffers['Accumulation'][1][..., 3] = 2
        self.assertFalse(assess(buffers)['checks']['singleFrameAccumulation'])

    def test_nonfinite_rejected(self):
        buffers = self.fixture()
        buffers['Accumulation'][1][0, 0, 0] = np.inf
        with self.assertRaises(ValueError):
            assess(buffers)

    def test_batch_radiance_is_normalized_by_sample_count(self):
        buffers = self.fixture()
        for name in RESOURCES:
            buffers[name][0]['width'] = 1
            buffers[name] = (buffers[name][0], buffers[name][1][:, :1].copy())
        buffers['Accumulation'][1][..., :3] *= 4
        buffers['Accumulation'][1][..., 3] = 4
        self.assertTrue(assess(buffers, 4, False)['passed'])
        self.assertFalse(assess(buffers, 2, False)['passed'])

    def test_batch_guides_require_identical_primary_sample(self):
        reference, current = self.fixture(), self.fixture()
        compare_primary_guides(reference, current)
        current['Albedo'][0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            compare_primary_guides(reference, current)
        current = self.fixture()
        current['Albedo'][1][0, 0, 0] += .25
        with self.assertRaises(ValueError):
            compare_primary_guides(reference, current)

    def test_zero_signals_do_not_pass_vacuously(self):
        buffers = self.fixture()
        for name in ('DiffuseRadianceHitT', 'SpecularRadianceHitT'):
            buffers[name][1][..., :3] = 0
        buffers['Accumulation'][1][..., :3] = 0
        result = assess(buffers)
        self.assertFalse(result['passed'])
        self.assertTrue(result['checks']['signalPartition'])
        self.assertFalse(result['checks']['nonzeroDiffuse'])


if __name__ == '__main__':
    unittest.main()
