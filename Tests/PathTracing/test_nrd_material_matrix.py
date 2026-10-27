import json
import unittest

import numpy as np

from validate_nrd_composition import NAMES, expected_composition
from validate_nrd_disocclusion import document_albedo
from validate_nrd_material_matrix import FIXTURE, MATERIALS, RESOURCES, RELAX, assess_materials, check_metadata


class MaterialMatrixTests(unittest.TestCase):
    def test_metadata_formats_and_cohort(self):
        common = None
        for name in RESOURCES:
            fmt = 10 if name in ('Albedo', 'SceneColor', 'MaterialF0Metallic', 'NormalRoughness') else 2
            meta = dict(resource='PathTracing.'+name, format=fmt, nrdPresentation=RELAX,
                nrdFailure='', nrdLastEvaluation=dict(recorded=True), sampleStartIndex=30)
            common = check_metadata(meta, name, common)
            for key, value in (('format', 2 if fmt == 10 else 10), ('sampleStartIndex', 31),
                    ('nrdFailure', 'failed'), ('nrdLastEvaluation', {})):
                with self.subTest(name=name, key=key), self.assertRaises(ValueError):
                    check_metadata(dict(meta, **{key: value}), name, common)

    def cohort(self):
        buffers = {name: np.zeros((1, 5, 4), dtype=np.float32) for name in RESOURCES}
        buffers['SceneColor'][..., 3] = 1
        for index, (_, color, metal, roughness) in enumerate(MATERIALS):
            albedo = document_albedo(color)
            buffers['Albedo'][0, index] = albedo
            buffers['MaterialF0Metallic'][0, index] = [*(albedo[:3] if metal else [.04]*3), metal]
            buffers['NormalRoughness'][0, index] = [0, 0, -1, roughness]
            buffers['NrdInputAdmission'][0, index] = [1, 0, metal, 0]
            buffers['NrdValidity'][0, index, 0] = 1
            for lobe in ('Diffuse', 'Specular'):
                value = 0 if lobe == 'Diffuse' and metal else .5
                buffers['Nrd'+lobe+'Factor'][0, index] = [.5, .5, .5, 1]
                buffers['Physical'+lobe][0, index, :3] = value
                buffers['Nrd'+lobe+'Candidate'][0, index, :3] = value*2
                buffers['NrdOutput'+lobe][0, index, :3] = value*2
        buffers['NrdSceneColor'], buffers['NrdCompositionDiagnostics'] = expected_composition(
            *(buffers[name] for name in NAMES[:8]))
        return buffers

    def test_complete_matrix(self):
        result = assess_materials(self.cohort(), minimum=1)
        self.assertEqual(len(result['materials']), 4)
        self.assertEqual(result['inactiveDiffusePixels'], 2)

    def test_missing_or_corrupt_materials_rejected(self):
        changes = (('Albedo', 0, 0, .99), ('MaterialF0Metallic', 2, 3, 0),
            ('MaterialF0Metallic', 3, 0, .04), ('NormalRoughness', 1, 3, .6),
            ('NrdInputAdmission', 2, 2, 0), ('PhysicalDiffuse', 3, 0, .1),
            ('NrdCompositionDiagnostics', 0, 0, 0), ('NrdInputAdmission', 4, 0, 1),
            ('NrdSceneColor', 4, 0, 1), ('NrdSceneColor', 1, 0, float('nan')))
        for resource, pixel, channel, value in changes:
            with self.subTest(resource=resource, pixel=pixel):
                buffers = self.cohort()
                buffers[resource][0, pixel, channel] = value
                with self.assertRaises(ValueError):
                    assess_materials(buffers, minimum=1)

    def test_missing_resource_or_insufficient_pixels_rejected(self):
        buffers = self.cohort()
        del buffers['MaterialF0Metallic']
        with self.assertRaises(ValueError):
            assess_materials(buffers, minimum=1)
        with self.assertRaises(ValueError):
            assess_materials(self.cohort(), minimum=2)

    def test_fixture_matches_contract(self):
        scene = json.loads((FIXTURE/'scene.json').read_text(encoding='utf-8'))
        self.assertEqual(len(scene['materials']), len(MATERIALS))
        for material, (name, color, metal, roughness) in zip(scene['materials'], MATERIALS):
            self.assertEqual(material['id'], name)
            self.assertEqual(material['baseColor'], list(color))
            self.assertEqual((material['metallic'], material['roughness']), (metal, roughness))
        self.assertEqual({n['materialId'] for n in scene['nodes']}, {m[0] for m in MATERIALS})


if __name__ == '__main__':
    unittest.main()
