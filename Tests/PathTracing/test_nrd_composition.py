import unittest

import numpy as np

from validate_nrd_composition import expected_composition, assess_composition, NAMES
from validate_nrd_composition_vectors import vectors


class CompositionTests(unittest.TestCase):
    def test_explicit_outcomes(self):
        cases, inputs, expected = vectors()
        self.assertEqual(len(cases), 17)
        self.assertEqual(expected[0, :4].tolist(), [5, 8, 14, 1])
        self.assertEqual(expected[1, :4].tolist(), [4, 7, 13, 1])
        self.assertEqual(expected[2, :4].tolist(), [4, 7, 13, 1])
        self.assertTrue(np.array_equal(expected[3, :4], inputs[3, 6]))
        self.assertEqual(expected[13, :4].tolist(), [0, 0, 0, 1])
        self.assertEqual(expected[14, 0], inputs[14, 6, 0])

    def test_missing_or_malformed_inputs(self):
        _, inputs, _ = vectors()
        args = [inputs[:, index] for index in range(7)] + [np.ones((17, 4), dtype=np.float32)]
        args[2] = args[2][:-1]
        with self.assertRaises(ValueError):
            expected_composition(*args)

    def cohort(self):
        buffers = {name: np.zeros((1, 2, 4), dtype=np.float32) for name in NAMES}
        buffers['SceneColor'][:] = [9, 8, 7, 1]
        buffers['NrdInputAdmission'][0] = [[1, 0, 0, 0], [0, 1, 0, 0]]
        buffers['NrdValidity'][0, 0] = [1, 1, 0, 0]
        for lobe in ('Diffuse', 'Specular'):
            buffers['Nrd'+lobe+'Factor'][0, 0] = [.5, .25, .125, 1]
            buffers['Nrd'+lobe+'Candidate'][0, 0] = [2, 4, 8, 3]
            buffers['Physical'+lobe][0, 0, :3] = [1, 1, 1]
            buffers['NrdOutput'+lobe][0, 0] = [2, 4, 8, 1]
        buffers['NrdSceneColor'], buffers['NrdCompositionDiagnostics'] = expected_composition(
            *(buffers[name] for name in NAMES[:8]))
        return buffers

    def test_runtime_cohort_and_corruption(self):
        self.assertEqual(assess_composition(self.cohort())['usedPixels'], 1)
        for name, pixel in (('NrdSceneColor', 0), ('NrdSceneColor', 1), ('NrdCompositionDiagnostics', 0),
                            ('NrdDiffuseFactor', 0), ('PhysicalSpecular', 0)):
            buffers = self.cohort()
            buffers[name][0, pixel, 0] += .1
            with self.assertRaises(ValueError):
                assess_composition(buffers)


if __name__ == '__main__':
    unittest.main()
