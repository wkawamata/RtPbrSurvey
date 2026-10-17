import copy
import unittest

import numpy as np

from validate_nrd_composition import NAMES, expected_composition
from validate_nrd_stable_radiance import RESOURCES, COLORS, EMISSION, RELAX, assess_stable, check_metadata


class StableRadianceTests(unittest.TestCase):
    def cohort(self):
        buffers = {name: np.zeros((1, 4, 4), dtype=np.float32) for name in RESOURCES}
        buffers['Albedo'][0, :3, :3] = COLORS
        buffers['Albedo'][0, :3, 3] = 1
        buffers['SceneColor'][..., 3] = 1
        buffers['SceneColor'][0, 1, :3] = np.float16(COLORS[1]).astype(np.float32)
        buffers['SceneColor'][0, 3, :3] = [.1, .2, .3]
        buffers['StableRadiance'][0, 0, :3] = EMISSION
        buffers['StableRadiance'][0, 1, :3] = COLORS[1]
        buffers['BackgroundRadiance'][0, 3, :3] = [.1, .2, .3]
        buffers['DenoiserPrimaryPosition'][0, :3, 3] = 1
        for pixel in (0, 2):
            buffers['NrdInputAdmission'][0, pixel] = [1, 0, 0, 0]
            buffers['NrdValidity'][0, pixel, 0] = 1
            for lobe in ('Diffuse', 'Specular'):
                buffers['Nrd'+lobe+'Factor'][0, pixel] = [.5, .5, .5, 1]
                buffers['Nrd'+lobe+'Candidate'][0, pixel, :3] = 1
                buffers['Physical'+lobe][0, pixel, :3] = .5
                buffers['NrdOutput'+lobe][0, pixel, :3] = 1
        buffers['NrdSceneColor'], buffers['NrdCompositionDiagnostics'] = expected_composition(
            *(buffers[name] for name in NAMES[:8]))
        return buffers

    def test_valid_cohort(self):
        result = assess_stable(self.cohort(), minimum=1)
        self.assertEqual(result['composedEmissivePixels'], 1)
        self.assertEqual(result['unlitPixels'], 1)
        self.assertEqual(result['usedPixels'], 2)

    def test_missing_and_insufficient_coverage(self):
        buffers = self.cohort()
        del buffers['Albedo']
        with self.assertRaises(ValueError):
            assess_stable(buffers, minimum=1)
        with self.assertRaises(ValueError):
            assess_stable(self.cohort(), minimum=2)

    def test_corrupt_inputs_and_composition(self):
        changes = (
            ('StableRadiance', 0, 0), ('StableRadiance', 0, 1.6),
            ('StableRadiance', 1, 1.1), ('PhysicalDiffuse', 1, .1),
            ('DenoiserPrimaryPosition', 1, 0), ('DenoiserDiffuseDistance', 1, 2),
            ('NrdInputAdmission', 1, 1), ('NrdValidity', 1, 1),
            ('NrdSceneColor', 1, 7), ('NrdSceneColor', 3, 7),
            ('BackgroundRadiance', 0, 1), ('NrdSceneColor', 0, 7),
        )
        for name, pixel, value in changes:
            with self.subTest(name=name, pixel=pixel, value=value):
                buffers = self.cohort()
                component = 3 if name == 'DenoiserPrimaryPosition' else 0
                buffers[name][0, pixel, component] = value
                with self.assertRaises(ValueError):
                    assess_stable(buffers, minimum=1)
        buffers = self.cohort()
        buffers['BackgroundRadiance'][:] = 0
        with self.assertRaises(ValueError):
            assess_stable(buffers, minimum=1)

    def test_metadata_is_required_and_consistent(self):
        meta = dict(resource='PathTracing.Albedo', format=10, nrdPresentation=RELAX,
            nrdFailure='', nrdLastEvaluation=dict(recorded=True), sampleStartIndex=30)
        common = check_metadata(meta, 'Albedo', None)
        self.assertEqual(check_metadata(meta, 'Albedo', common), common)
        for key, value in (('resource', 'PathTracing.SceneColor'), ('format', 2),
                ('nrdFailure', 'failed'), ('nrdLastEvaluation', {}),
                ('nrdPresentation', 'Native PT'), ('sampleStartIndex', 31)):
            other = copy.deepcopy(meta)
            other[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                check_metadata(other, 'Albedo', common)


if __name__ == '__main__':
    unittest.main()
