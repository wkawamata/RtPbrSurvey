import unittest

import numpy as np

from validate_denoiser_outputs import assess, assess_progressive, FORMATS, RESOURCES


class DenoiserOutputTests(unittest.TestCase):
    def fixture(self):
        values = (
            [[[.5, .5, .5, 1], [0, 0, 0, 0]]],
            [[[.25, .25, .25, 5], [0, 0, 0, 0]]],
            [[[.125, .125, .125, 5], [0, 0, 0, 0]]],
            [[[.375, .375, .375, 1], [2, 2, 2, 1]]],
            [[[.375, .375, .375, 1], [2, 2, 2, 1]]],
            [[[.04, .04, .04, 0], [0, 0, 0, 0]]],
            [[[0, 0, 0, 0], [0, 0, 0, 0]]],
            [[[0, 0, 0, 0], [2, 2, 2, 0]]])
        return {name: (dict(resource='PathTracing.' + name, format=fmt,
                           width=2, height=1, randomSeed=7, sampleStartIndex=29,
                           viewProjection=[1], previousViewProjection=[1]),
                       np.array(value, dtype=float))
                for name, fmt, value in zip(RESOURCES, FORMATS, values)}

    def test_reconstruction_and_background(self):
        self.assertTrue(assess(self.fixture())['passed'])

    def test_required_resources(self):
        buffers = self.fixture()
        del buffers['StableRadiance']
        with self.assertRaises(ValueError):
            assess(buffers)

    def test_frame_format_and_nonfinite_rejected(self):
        for key, value in [('sampleStartIndex', 30), ('format', 10)]:
            buffers = self.fixture()
            buffers['CurrentFrameRadiance'][0][key] = value
            with self.assertRaises(ValueError):
                assess(buffers)
        buffers = self.fixture()
        buffers['StableRadiance'][1][0, 0, 0] = np.nan
        with self.assertRaises(ValueError):
            assess(buffers)

    def test_bad_partition_and_accumulation_rejected(self):
        buffers = self.fixture()
        buffers['DiffuseRadianceHitT'][1][0, 0, 0] += 1
        self.assertFalse(assess(buffers)['checks']['reconstruction'])
        buffers = self.fixture()
        buffers['Accumulation'][1][..., 3] = 10
        self.assertFalse(assess(buffers)['checks']['currentNotAccumulated'])

    def test_material_and_background_sentinels(self):
        buffers = self.fixture()
        buffers['MaterialF0Metallic'][1][0, 0, 0] = .4
        self.assertFalse(assess(buffers)['checks']['materialF0'])
        buffers = self.fixture()
        buffers['BackgroundRadiance'][1][0, 0, 0] = 1
        self.assertFalse(assess(buffers)['checks']['backgroundHit'])

    def test_stable_signal_cannot_exceed_legacy_diffuse(self):
        buffers = self.fixture()
        buffers['StableRadiance'][1][0, 0, 0] = 1
        self.assertFalse(assess(buffers)['checks']['nonnegativeFilteredDiffuse'])

    def test_primary_stable_fixture_requires_visible_materials(self):
        with self.assertRaises(ValueError):
            assess(self.fixture(), True)

    def stable_fixture(self):
        buffers = self.fixture()
        for name in RESOURCES:
            meta, values = buffers[name]
            meta['width'] = 600
            buffers[name] = (meta, np.repeat(values[:, 1:2], 600, axis=1))
        for start, value in [(0, [2, .5, .25]), (300, [.1, .2, .3])]:
            region = slice(start, start + 200)
            buffers['Albedo'][1][:, region] = [.5, .5, .5, 1]
            buffers['MaterialF0Metallic'][1][:, region] = [.04, .04, .04, 0]
            for name in ('CurrentFrameRadiance', 'Accumulation'):
                buffers[name][1][:, region] = [*value, 1]
            for name in ('StableRadiance', 'DiffuseRadianceHitT'):
                buffers[name][1][:, region] = [*value, 0]
            buffers['BackgroundRadiance'][1][:, region] = 0
        return buffers

    def test_primary_emission_independent_values(self):
        self.assertTrue(assess(self.stable_fixture(), True)['passed'])
        buffers = self.stable_fixture()
        buffers['StableRadiance'][1][0, 0, 0] += .1
        self.assertFalse(assess(buffers, True)['checks']['primaryEmission'])
        buffers = self.stable_fixture()
        buffers['StableRadiance'][1][0, 300, 0] += .1
        self.assertFalse(assess(buffers, True)['checks']['secondPrimaryEmission'])

    def test_progressive_current_is_history_independent(self):
        reference = self.fixture()['CurrentFrameRadiance']
        buffers = {name: self.fixture()[name] for name in ('CurrentFrameRadiance', 'Accumulation')}
        buffers['Accumulation'][1][..., :3] *= 3
        buffers['Accumulation'][1][..., 3] = 2
        self.assertTrue(assess_progressive(buffers, reference)['passed'])
        buffers['CurrentFrameRadiance'][1][0, 0, 0] += 1
        self.assertFalse(assess_progressive(buffers, reference)['passed'])

    def test_progressive_wrong_frame_and_nonfinite_rejected(self):
        reference = self.fixture()['CurrentFrameRadiance']
        buffers = {name: self.fixture()[name] for name in ('CurrentFrameRadiance', 'Accumulation')}
        buffers['Accumulation'][0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            assess_progressive(buffers, reference)
        buffers = {name: self.fixture()[name] for name in ('CurrentFrameRadiance', 'Accumulation')}
        buffers['Accumulation'][1][0, 0, 0] = np.inf
        with self.assertRaises(ValueError):
            assess_progressive(buffers, reference)


if __name__ == '__main__':
    unittest.main()
