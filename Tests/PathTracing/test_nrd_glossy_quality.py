import copy
import json
import unittest

import numpy as np

from validate_nrd_disocclusion import document_albedo
from validate_nrd_glossy_quality import (FIXTURE, LIMITS, MATERIALS, RESOURCES,
    assess_environment_control, assess_reflection, assess_scene, interior)


class GlossyQualityTests(unittest.TestCase):
    def test_environment_control_is_required(self):
        meta = dict(resource='PathTracing.SceneColor', format=10, nrdPreviewRequested=False,
            nrdPresentation='Native PT', primarySampling='pixel-center', samplesPerFrame=1, sampleStartIndex=30)
        image = np.zeros((10, 10, 4), dtype=np.float32)
        image[..., 3] = 1
        self.assertTrue(assess_environment_control(meta, image)['passed'])
        image[0, 0, 0] = .1
        self.assertFalse(assess_environment_control(meta, image)['passed'])
        for key, value in (('resource', 'PathTracing.NrdSceneColor'), ('nrdPreviewRequested', True),
                ('sampleStartIndex', 0), ('format', 2)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                assess_environment_control(dict(meta, **{key: value}), image)

    def signals(self):
        reference = np.repeat(np.linspace(.05, 1, 200)[:, None], 3, axis=1)
        native = reference.copy()
        native[::2] *= 1.1
        native[1::2] *= .9
        return reference, native, reference, reference

    def cohort(self):
        buffers = {name: np.zeros((45, 45, 4), dtype=np.float32) for name in RESOURCES}
        buffers['SceneColor'][..., 3] = 1
        buffers['NrdSceneColor'][..., 3] = 1
        reference = buffers['SceneColor'].copy()
        for i, (_, color, _, _) in enumerate(MATERIALS):
            y, x = (1+22*(i//2), 1+22*(i % 2))
            index = np.s_[y:y+20, x:x+20]
            buffers['Albedo'][index] = document_albedo(color)
            buffers['NrdInputAdmission'][index] = [1, 0, 0, 0]
            buffers['NrdCompositionDiagnostics'][index] = [1, 0, 0, 0]
            values = np.tile(np.linspace(.05, 1, 20)[None, :, None], (20, 1, 3))
            reference[index][..., :3] = values
            buffers['NrdSceneColor'][index][..., :3] = values
            values = values.copy()
            values[::2] *= 1.1
            values[1::2] *= .9
            buffers['SceneColor'][index][..., :3] = values
        return buffers, [reference.copy(), reference.copy()]

    def test_qualified_quality_pass(self):
        result = assess_reflection(*self.signals())
        self.assertTrue(result['passed'])
        verdict, rows = assess_scene(*self.cohort())
        self.assertEqual(verdict, 'passed-static-glossy-fixture')
        self.assertEqual({r['material'] for r in rows}, {m[0] for m in MATERIALS})

    def test_regression_and_bias_are_not_accepted(self):
        signals = list(self.signals())
        signals[0] = signals[0]*1.2
        result = assess_reflection(*signals)
        self.assertFalse(result['passed'])
        self.assertFalse(result['checks']['meanRgbBias'])
        self.assertFalse(result['checks']['noRmseRegression'])

    def test_unqualified_references_and_fixture(self):
        buffers, references = self.cohort()
        references[1] *= 1.2
        verdict, _ = assess_scene(buffers, references)
        self.assertEqual(verdict, 'inconclusive-reference')
        buffers, references = self.cohort()
        for image in references:
            image[..., :3] = .5
        verdict, _ = assess_scene(buffers, references)
        self.assertEqual(verdict, 'inconclusive-fixture')

    def test_missing_identity_coverage_and_fallback_rejected(self):
        base, references = self.cohort()
        for mutation in ('resource', 'material', 'coverage', 'admission', 'fallback', 'nan', 'reference'):
            with self.subTest(mutation=mutation):
                buffers, refs = copy.deepcopy(base), copy.deepcopy(references)
                if mutation == 'resource':
                    del buffers['Albedo']
                elif mutation == 'material':
                    buffers['Albedo'][1:21, 1:21] = [.99, .99, .99, 1]
                elif mutation == 'coverage':
                    buffers['NrdCompositionDiagnostics'][1:21, 1:21] = 0
                elif mutation == 'admission':
                    buffers['NrdInputAdmission'][1:21, 1:21] = 0
                elif mutation == 'fallback':
                    buffers['NrdSceneColor'][0, 0, 0] = 1
                elif mutation == 'nan':
                    buffers['NrdSceneColor'][5, 5, 0] = float('nan')
                else:
                    refs.pop()
                with self.assertRaises(ValueError):
                    assess_scene(buffers, refs)

    def test_bad_or_dark_regions_rejected(self):
        signals = self.signals()
        for values in ([a[:99] for a in signals], [np.zeros_like(a) for a in signals],
                [np.full_like(a, np.inf) for a in signals]):
            with self.assertRaises(ValueError):
                assess_reflection(*values)

    def test_erosion_and_fixed_fixture(self):
        self.assertEqual(int(interior(np.ones((20, 20), dtype=bool)).sum()), 196)
        scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))
        self.assertEqual(len(scene['nodes']), 4)
        self.assertTrue(all(n['primitive']['kind'] == 'sphere' for n in scene['nodes']))
        self.assertEqual({(m['metallic'], m['roughness']) for m in scene['materials']},
            {(0, .6), (0, .1), (1, .6), (1, .1)})
        self.assertEqual(LIMITS, dict(referencePairNrmse=.03, meanRgbBias=.05,
            nrdToNativeRmse=1.0, minimumContrast=2.0, minimumComposedFraction=.9, minimumPixels=100))


if __name__ == '__main__':
    unittest.main()
