import unittest

import numpy as np

import test_secondary_geometry
from validate_denoiser_distances import assess, RESOURCES, sample_lobes
from validate_secondary_geometry import planes_from_scene, primary_rays, intersect_planes


class DenoiserDistanceTests(unittest.TestCase):
    def fixture(self):
        base = test_secondary_geometry.SecondaryGeometryTests()
        scene = base.scene()
        scene['nodes'][1]['primitive'].update(width=2, depth=2)
        scene['materials'] = [dict(baseColor=[.5, .5, .5, 1], metallic=.35, roughness=.55)]
        meta = dict(base.fixture()['Albedo'][0], denoiserDistanceDiagnostics=True)
        buffers = {name: (dict(meta, resource='PathTracing.'+name, format=10 if name == 'Albedo' else 2),
            np.zeros((128, 128, 4))) for name in RESOURCES}
        buffers['Albedo'][1][..., 3] = 1
        ys, xs = np.mgrid[:128, :128]
        xs, ys = xs.ravel(), ys.ravel()
        planes = planes_from_scene(scene)
        origins, primary_dir = primary_rays(meta, xs, ys)
        primary_t, index, edge = intersect_planes(origins, primary_dir, planes, .001, 100)
        point = origins+primary_dir*primary_t[:, None]
        buffers['DenoiserPrimaryPosition'][1][ys, xs] = np.column_stack((point, np.ones(len(xs))))
        normal = np.asarray([planes[i][3] for i in index])
        for lobe, (direction, pdf) in zip(('Diffuse', 'Specular'),
                sample_lobes(meta, xs, ys, normal, -primary_dir, .55)):
            buffers['Denoiser'+lobe+'Direction'][1][ys, xs] = np.column_stack((direction, pdf))
            biased_origin = (point.astype(np.float32)+normal.astype(np.float32)*np.float32(.001)).astype(float)
            t, index, edge = intersect_planes(biased_origin, direction, planes, .001, 100)
            valid = (pdf > 0) & (np.sum(direction*normal, axis=-1) > 0)
            hit = valid & (index >= 0)
            data = buffers['Denoiser'+lobe+'Distance'][1]
            data[ys, xs, 1] = np.where(valid, np.where(hit, 1, 2), 3)
            data[ys[valid & ~hit], xs[valid & ~hit], 2] = 100
            data[ys[hit], xs[hit], 2] = t[hit]
            data[ys[hit], xs[hit], 0] = np.linalg.norm((biased_origin-point)[hit]+direction[hit]*t[hit, None], axis=-1)
        return buffers, scene

    def test_independent_fixture_passes(self):
        buffers, scene = self.fixture()
        self.assertTrue(assess(buffers, scene)['passed'])

    def test_changed_direction_and_pdf_are_rejected(self):
        buffers, scene = self.fixture()
        buffers['DenoiserDiffuseDirection'][1][..., 0] += .1
        buffers['DenoiserSpecularDirection'][1][..., 3] += .1
        report = assess(buffers, scene)
        self.assertFalse(report['checks']['DiffuseDirection'])
        self.assertFalse(report['checks']['SpecularPdf'])

    def test_primary_distance_substitution_fails(self):
        buffers, scene = self.fixture()
        buffers['DenoiserDiffuseDistance'][1][..., 0] = 1
        self.assertFalse(assess(buffers, scene)['checks']['DiffuseDistance'])

    def test_invalid_direction_cannot_become_a_miss(self):
        buffers, scene = self.fixture()
        data = buffers['DenoiserSpecularDistance'][1]
        self.assertTrue((data[..., 1] == 3).any())
        data[..., 1][data[..., 1] == 3] = 2
        self.assertFalse(assess(buffers, scene)['checks']['SpecularState'])

    def test_disabled_and_mismatched_cohorts_are_rejected(self):
        buffers, scene = self.fixture()
        buffers['Albedo'][0]['denoiserDistanceDiagnostics'] = False
        with self.assertRaises(ValueError):
            assess(buffers, scene)
        buffers['Albedo'][0]['denoiserDistanceDiagnostics'] = True
        buffers['DenoiserSpecularDistance'][0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            assess(buffers, scene)

    def test_recorded_position_is_independently_checked(self):
        buffers, scene = self.fixture()
        buffers['DenoiserPrimaryPosition'][1][..., 0] += .01
        self.assertFalse(assess(buffers, scene)['checks']['primaryPosition'])


if __name__ == '__main__':
    unittest.main()
