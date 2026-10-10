import unittest
from unittest.mock import patch
import numpy as np
from validate_hit_miss_guides import partition_error, sample_hits, uncertain_sample_hits, assess
from validate_inputs import srgb_texture_material
from validate_guide_contract import RESOURCES, FORMATS
from make_boundary_fixture import BACKGROUND


class HitMissTests(unittest.TestCase):
    def mixed_fixture(self):
        count = 1004
        hits = np.zeros((2, count), dtype=bool)
        hits[1, 0], hits[0, 1], hits[:, 2:4] = True, True, True
        first, any_hit = hits[0], hits.any(axis=0)
        color, roughness, _ = srgb_texture_material([.25, .5, .75], .37)
        channels = (4, 1, 2, 4, 4, 4, 4)
        values = [np.zeros((1, count, channel)) for channel in channels]
        normal, depth, _, albedo, diffuse, specular, accumulation = values
        normal[0, :, 3] = 1
        normal[0, first] = [0, 0, -1, roughness]
        depth[0, first, 0] = 5
        albedo[0, first] = [*color, 1]
        diffuse[0, any_hit, :3] = [.1, .2, .3]
        specular[0, any_hit, :3] = [.05, .05, .05]
        diffuse[0, first, 3] = specular[0, first, 3] = 5
        accumulation[0, :, :3] = (diffuse[0, :, :3]+specular[0, :, :3] +
            (1-hits.mean(axis=0))[:, None]*BACKGROUND)*2
        accumulation[0, :, 3] = 2
        buffers = {name: (dict(resource='PathTracing.'+name, format=fmt, width=count, height=1,
            sampleStartIndex=60, randomSeed=7, viewProjection=[1], previousViewProjection=[1]), value)
            for name, fmt, value in zip(RESOURCES, FORMATS, values)}
        return buffers, hits

    def evaluate_fixture(self, buffers, hits):
        with patch('validate_hit_miss_guides.analysis_pixels', return_value=(np.arange(1004), np.zeros(1004, dtype=int))), \
             patch('validate_hit_miss_guides.sample_hits', return_value=hits), \
             patch('validate_hit_miss_guides.uncertain_sample_hits', return_value=np.zeros_like(hits)):
            return assess(buffers, 2)

    def test_first_miss_does_not_discard_later_surface_signal(self):
        buffers, hits = self.mixed_fixture()
        self.assertTrue(self.evaluate_fixture(buffers, hits)['passed'])
        buffers['DiffuseRadianceHitT'][1][0, 0, :3] = 0
        buffers['SpecularRadianceHitT'][1][0, 0, :3] = 0
        result = self.evaluate_fixture(buffers, hits)
        self.assertFalse(result['checks']['laterSurfaceSignalsSurviveFirstMiss'])
        self.assertFalse(result['checks']['partitionIncludesAllMissSamples'])

    def test_first_miss_keeps_zero_hit_t(self):
        buffers, hits = self.mixed_fixture()
        buffers['DiffuseRadianceHitT'][1][0, 0, 3] = 5
        buffers['SpecularRadianceHitT'][1][0, 0, 3] = 5
        self.assertFalse(self.evaluate_fixture(buffers, hits)['checks']['firstMissHitTZero'])

    def test_first_sample_validity_not_averaged(self):
        buffers, hits = self.mixed_fixture()
        buffers['Albedo'][1][0, 0, 3] = .5
        self.assertFalse(self.evaluate_fixture(buffers, hits)['checks']['firstSampleValidity'])

    def test_finite_plane_hit_mask(self):
        meta = dict(width=8, height=8, sampleStartIndex=60, randomSeed=7,
                    inverseViewProjection=np.eye(4).tolist())
        hits = sample_hits(meta, np.array([0, 4, 8]), np.array([4, 4, 4]), 4)
        np.testing.assert_array_equal(hits, [[True, True, False]]*4)

    def test_miss_background_fraction_not_first_sample_only(self):
        hits = np.array([[False, True], [True, False]])
        diffuse = np.array([[.1, .2, .3, 0], [.1, .2, .3, 5]])
        specular = np.zeros((2, 4))
        accumulation = np.column_stack(((diffuse[:, :3]+np.array(BACKGROUND)/2)*2, [2, 2]))
        error, bound = partition_error(diffuse, specular, accumulation, hits, 2)
        self.assertTrue((error <= bound).all())
        wrong = hits.copy()
        wrong[1] = wrong[0]
        error, bound = partition_error(diffuse, specular, accumulation, wrong, 2)
        self.assertFalse((error <= bound).all())

    def test_all_miss_and_all_hit_limits(self):
        for hit in (False, True):
            hits = np.full((4, 1), hit)
            surface = np.full((1, 4), .25 if hit else 0)
            accumulation = np.column_stack(((surface[:, :3]+(0 if hit else np.array(BACKGROUND)))*4, [4]))
            error, bound = partition_error(surface, np.zeros((1, 4)), accumulation, hits, 4)
            self.assertTrue((error <= bound).all())

    def test_uncertainty_is_local_and_does_not_relax_radiance_bound(self):
        hits = np.array([[False], [False], [False], [False]])
        uncertain = np.array([[False], [False], [True], [False]])
        diffuse, specular = np.zeros((1, 4)), np.zeros((1, 4))
        accumulation = np.array([[*np.array(BACKGROUND)*3, 4]])
        error, bound = partition_error(diffuse, specular, accumulation, hits, 4)
        self.assertFalse((error <= bound).all())
        error, bound = partition_error(diffuse, specular, accumulation, hits, 4, uncertain)
        self.assertTrue((error <= bound).all())
        accumulation[0, 2] = 100
        error, bound = partition_error(diffuse, specular, accumulation, hits, 4, uncertain)
        self.assertFalse((error <= bound).all())

    def test_far_from_geometry_edges_is_not_uncertain(self):
        meta = dict(width=8, height=8, sampleStartIndex=60, randomSeed=7,
                    inverseViewProjection=np.eye(4).tolist())
        uncertain = uncertain_sample_hits(meta, np.array([0, 4, 8]), np.array([4, 4, 4]), 4)
        self.assertFalse(uncertain.any())


if __name__ == '__main__':
    unittest.main()
