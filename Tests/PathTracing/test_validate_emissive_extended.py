import base64
import io
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image
from validate_emissive import rectangle_integral, reference
from validate_emissive_extended import emitters_for, make_fixture


class ExtendedEmissiveTests(unittest.TestCase):
    def test_emitter_size_matches_instance_scale(self):
        for case, half_size in [('small', .15), ('large', 4.5)]:
            with tempfile.TemporaryDirectory() as directory:
                scene, _, emitters = make_fixture(Path(directory), case)
                self.assertEqual(emitters[0]['half_size'], half_size)
                self.assertAlmostEqual(scene['nodes'][1]['scale'][0]*1.5, half_size)
                self.assertAlmostEqual(scene['nodes'][1]['scale'][2]*1.5, half_size)
                self.assertEqual(scene['nodes'][1]['scale'][1], 1)
                oracle = reference(scene, emitters)
                self.assertLess(oracle['quadratureRelativeChange'], .002)

    def test_area_reference_increases_with_size(self):
        values = []
        for case in ['small', 'large']:
            emitter = emitters_for(case)[0]
            value = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 128, **emitter)
            values.append(value)
        self.assertTrue(np.all(values[0] > 0))
        self.assertTrue(np.all(values[1] > values[0]))

    def test_unequal_area_fixture_keeps_both_instances(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, preset, emitters = make_fixture(Path(directory), 'unequal')
            self.assertEqual(len(scene['assets']), 2)
            self.assertEqual(len(scene['nodes']), 3)
            self.assertAlmostEqual((emitters[0]['half_size']/emitters[1]['half_size'])**2, 4)
            self.assertNotEqual(emitters[0]['emission'], emitters[1]['emission'])
            self.assertFalse(preset['pathTracing']['environmentEnabled'])
            oracle = reference(scene, emitters)
            self.assertGreater(min(oracle['rgb']), 0)
            self.assertLess(oracle['quadratureRelativeChange'], .002)

    def test_texture_has_uvs_and_black_white_texels(self):
        with tempfile.TemporaryDirectory() as directory:
            make_fixture(Path(directory), 'texture')
            gltf = json.loads((Path(directory)/'emitter-0.gltf').read_text())
            self.assertEqual(gltf['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0'], 3)
            self.assertEqual(gltf['bufferViews'][3]['byteOffset'], 96)
            png = base64.b64decode(gltf['images'][0]['uri'].split(',')[1])
            image = Image.open(io.BytesIO(png))
            self.assertEqual(image.size, (2, 2))
            self.assertEqual(image.getpixel((0, 0)), (0, 0, 0, 255))
            self.assertEqual(image.getpixel((1, 0)), (255, 255, 255, 255))

    def test_textured_integral_is_dimmer_and_converges(self):
        emitter = emitters_for('texture')[0]
        low = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 32, **emitter)
        high = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 64, **emitter)
        white = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 64)
        np.testing.assert_allclose(low, high, rtol=1e-4)
        self.assertTrue(np.all(high > 0))
        self.assertTrue(np.all(high < white))
        np.testing.assert_allclose(high/high[2], [4, 2, 1])

    def test_archived_fixtures_match_generated_geometry(self):
        root = Path(__file__).resolve().parents[2]/'Assets/Scene/PathTracingValidation'
        for case, name in [('unequal', '03-unequal-emissive-panels'),
                ('texture', '04-textured-emissive-panel'),
                ('small', '09-small-emissive-panel'), ('large', '10-large-emissive-panel')]:
            folder = root/name
            archived = json.loads((folder/'scene.json').read_text(encoding='utf-8'))
            with tempfile.TemporaryDirectory() as directory:
                scene, _, _ = make_fixture(Path(directory), case)
                self.assertEqual(archived['nodes'], scene['nodes'])
                for asset in scene['assets']:
                    self.assertEqual(json.loads((folder/asset['path']).read_text()),
                        json.loads((Path(directory)/asset['path']).read_text()))
            self.assertTrue(archived['description'])
            preset = json.loads((folder/'render-preset.json').read_text())
            self.assertTrue(preset['pathTracing']['emissiveEnabled'])
            result = json.loads((folder/'evaluation-results.json').read_text())
            self.assertEqual(result['status'], 'passed')


if __name__ == '__main__':
    unittest.main()
