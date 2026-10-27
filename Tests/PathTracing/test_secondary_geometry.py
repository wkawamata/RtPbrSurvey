import unittest

import numpy as np

from validate_secondary_geometry import planes_from_scene, intersect_planes, assess, RESOURCES


class SecondaryGeometryTests(unittest.TestCase):
    def scene(self):
        def plane(name, z, sign):
            return dict(id=name, type='primitive', parentId=None, visible=True,
                translation=[0, 0, z], scale=[1, 1, 1],
                rotation=[sign*np.sqrt(.5), 0, 0, np.sqrt(.5)],
                primitive=dict(kind='plane', width=10, depth=10))
        return dict(assets=[], nodes=[plane('primary', 1, -1), plane('secondary', -1, 1)])

    def fixture(self):
        buffers = {}
        matrix = np.eye(4).tolist()
        for name in RESOURCES:
            values = np.zeros((128, 128, 4))
            meta = dict(resource='PathTracing.'+name, format=10 if name == 'Albedo' else 2,
                width=128, height=128, randomSeed=7, sampleStartIndex=30,
                viewProjection=matrix, previousViewProjection=matrix, inverseViewProjection=matrix,
                primarySampling='pixel-center', samplesPerFrame=1, rayTMin=.001, rayTMax=100., normalBias=.001)
            if name == 'Albedo':
                values[..., 3] = 1
            elif name == 'PrimaryContinuation':
                values[:, :64, 2] = -1
                values[:, 64:, 0] = 1
                values[..., 3] = .5
            elif name == 'SecondarySurfaceDistance':
                values[:, :64] = [2, 1, 0, 0]
                values[:, 64:] = [0, 2, 0, 0]
            else:
                values[:, :64] = [1.999, 1, 1, 0]
                values[:, 64:] = [100, 2, 2, 0]
            buffers[name] = (meta, values)
        return buffers

    def test_quaternion_plane_axes_and_front_face_culling(self):
        planes = planes_from_scene(self.scene())
        np.testing.assert_allclose(planes[0][3], [0, 0, -1], atol=1e-15)
        distance, index, edge = intersect_planes(np.array([[0., 0., 0.]]),
            np.array([[0., 0., 1.]]), planes, .001, 100)
        np.testing.assert_allclose(distance, [1])
        np.testing.assert_array_equal(index, [0])
        self.assertFalse(edge.any())

    def test_finite_bounds_near_and_far_limits(self):
        planes = planes_from_scene(self.scene())
        origins = np.array([[6., 0., 0.], [0., 0., 0.]])
        directions = np.array([[0., 0., 1.], [0., 0., 1.]])
        self.assertTrue(np.isinf(intersect_planes(origins, directions, planes, .001, .5)[0]).all())
        self.assertTrue(np.isinf(intersect_planes(origins[:1], directions[:1], planes, .001, 100)[0]).all())
        self.assertTrue(np.isinf(intersect_planes(origins[1:], directions[1:], planes, 2, 100)[0]).all())

    def test_fixed_boundary_margin(self):
        plane = planes_from_scene(self.scene())[:1]
        _, _, edge = intersect_planes(np.array([[4.999, 0., 0.], [0., 0., 0.]]),
            np.array([[0., 0., 1.], [0., 0., 1.]]), plane, .001, 100)
        np.testing.assert_array_equal(edge, [True, False])

    def test_supported_geometry_only(self):
        for key, value in [('parentId', 'parent'), ('scale', [-1, 1, 1]), ('rotation', [0, 0, 0, 0])]:
            scene = self.scene()
            scene['nodes'][0][key] = value
            with self.assertRaises(ValueError):
                planes_from_scene(scene)
        scene = self.scene()
        scene['nodes'][0]['primitive']['kind'] = 'sphere'
        with self.assertRaises(ValueError):
            planes_from_scene(scene)

    def test_geometry_hit_distance_and_miss_sentinel(self):
        result = assess(self.fixture(), self.scene())
        self.assertTrue(result['passed'], result)
        self.assertGreaterEqual(result['hitPixels'], 100)
        self.assertGreaterEqual(result['missPixels'], 100)
        self.assertAlmostEqual(result['biasCorrectionMax'], .001, places=12)
        self.assertLess(result['surfaceDistanceMaxError'], 1e-12)

    def test_camera_to_primary_distance_is_not_secondary_distance(self):
        buffers = self.fixture()
        buffers['SecondaryRayData'][1][:, :64, 0] = 1
        self.assertFalse(assess(buffers, self.scene())['checks']['secondaryDistance'])

    def test_published_distance_cannot_reuse_biased_hit_parameter(self):
        buffers = self.fixture()
        buffers['SecondarySurfaceDistance'][1][:, :64, 0] = 1.999
        self.assertFalse(assess(buffers, self.scene())['checks']['publishedSurfaceDistance'])

    def test_published_miss_does_not_fabricate_finite_distance(self):
        buffers = self.fixture()
        buffers['SecondarySurfaceDistance'][1][:, 64:, 0] = 100
        self.assertFalse(assess(buffers, self.scene())['checks']['surfaceNoHitZero'])

    def test_wrong_hit_status_and_miss_distance_fail(self):
        buffers = self.fixture()
        buffers['SecondaryRayData'][1][:, :64, 2] = 2
        self.assertFalse(assess(buffers, self.scene())['checks']['secondaryStatus'])
        buffers = self.fixture()
        buffers['SecondaryRayData'][1][:, 64:, 0] = 0
        self.assertFalse(assess(buffers, self.scene())['checks']['missSentinel'])

    def test_normal_bias_is_part_of_the_distance_origin(self):
        buffers = self.fixture()
        for meta, _ in buffers.values():
            meta['normalBias'] = .1
        self.assertFalse(assess(buffers, self.scene())['checks']['secondaryDistance'])

    def test_cohort_and_parameters_are_required(self):
        for key, value in [('rayTMax', 0), ('normalBias', float('nan')),
                           ('samplesPerFrame', 4), ('primarySampling', 'random-per-pixel')]:
            buffers = self.fixture()
            buffers['SecondaryRayData'][0][key] = value
            with self.assertRaises(ValueError):
                assess(buffers, self.scene())
        buffers = self.fixture()
        buffers['Albedo'][0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            assess(buffers, self.scene())
        buffers = self.fixture()
        del buffers['PrimaryContinuation']
        with self.assertRaises(ValueError):
            assess(buffers, self.scene())


if __name__ == '__main__':
    unittest.main()
