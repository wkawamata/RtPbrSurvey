import copy
import json
import unittest

import numpy as np

from validate_nrd_disocclusion import (FIXTURE, RELAX, check_cohort, document_albedo, visibility_regions,
    assess_visibility, measure_color)


class DisocclusionTests(unittest.TestCase):
    def setUp(self):
        self.scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))

    def metadata(self, previous_x=0):
        f = 1/np.tan(np.radians(55)/2)
        projection = np.array([[f, 0, 0, 0], [0, f, 0, 0],
            [0, 0, 1000/999.9, 1], [0, 0, -100/999.9, 0.]])
        view = np.eye(4)
        view[3, 2] = 6
        prior = view.copy()
        prior[3, 0] = -previous_x
        vp, pvp = (view@projection).T, (prior@projection).T
        return dict(resource='PathTracing.Albedo', format=10, width=256, height=256,
            primarySampling='pixel-center', rayTMin=.001, rayTMax=1000,
            viewProjection=vp.tolist(), previousViewProjection=pvp.tolist(),
            inverseViewProjection=np.linalg.inv(vp).tolist())

    def albedo(self, meta):
        xs, ys, indices, _, _, _ = visibility_regions(meta, self.scene)
        image = np.zeros((256, 256, 4))
        for index in (0, 1):
            mask = indices == index
            image[ys[mask], xs[mask]] = np.asarray(document_albedo(
                self.scene['materials'][index]['baseColor']), dtype=np.float16)
        return image

    def test_static_and_both_directions(self):
        for previous_x in (0, -1, 1):
            meta = self.metadata(previous_x)
            result = assess_visibility(meta, self.albedo(meta), self.scene, previous_x != 0)
            self.assertGreater(result['sampledRetainedPixels'], 100)
            if previous_x:
                self.assertGreater(result['sampledRevealedPixels'], 20)
            else:
                self.assertEqual(result['sampledRevealedPixels'], 0)

    def test_no_motion_and_wrong_material_are_rejected(self):
        meta = self.metadata()
        with self.assertRaises(ValueError):
            assess_visibility(meta, self.albedo(meta), self.scene, True)
        meta = self.metadata(1)
        image = self.albedo(meta)
        image[..., :3] = .4
        with self.assertRaises(ValueError):
            assess_visibility(meta, image, self.scene, True)

    def test_document_color_uses_srgb8_roundtrip(self):
        observed = document_albedo([.8, .03, .7, 1])
        encoded = np.array([231, 48, 218])/255
        expected = ((encoded+.055)/1.055)**2.4
        np.testing.assert_allclose(observed[:3], expected, atol=1e-12, rtol=0)
        self.assertEqual(observed[3], 1)

    def test_color_is_measurement_not_acceptance(self):
        meta = self.metadata(1)
        native = np.ones((256, 256, 4))
        contaminated = native.copy()
        contaminated[..., 0] = 100
        result = measure_color(meta, self.scene, contaminated, native)
        self.assertEqual(result['qualityVerdict'], 'not-evaluated')
        self.assertGreater(result['revealed']['meanAbsoluteDifference'], 0)
        contaminated[0, 0, 0] = float('nan')
        with self.assertRaises(ValueError):
            measure_color(meta, self.scene, contaminated, native)

    def test_identity_runtime_and_cohort(self):
        meta = dict(resource='PathTracing.MotionVectors', format=34, sampleStartIndex=0,
            nrdPresentation=RELAX, nrdFailure='', nrdLastEvaluation=dict(recorded=True))
        cohort = check_cohort(meta, 'MotionVectors', None)
        self.assertEqual(check_cohort(meta, 'MotionVectors', cohort), cohort)
        for key, value in (('resource', 'PathTracing.Albedo'), ('format', 2),
                ('nrdFailure', 'failed'), ('nrdLastEvaluation', {}), ('sampleStartIndex', 2)):
            other = copy.deepcopy(meta)
            other[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_cohort(other, 'MotionVectors', cohort)


if __name__ == '__main__':
    unittest.main()
