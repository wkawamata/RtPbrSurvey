import base64
import json
import struct
import unittest
import zlib
import numpy as np
from make_textured_fixture import COLORS, quadrant_png, make_gltf
from validate_textured_guides import expected_regions
from validate_inputs import ROOT


class TexturedGuideTests(unittest.TestCase):
    def test_png_contains_exact_quadrants(self):
        png = quadrant_png(COLORS)
        self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
        offset, data = 8, b''
        while offset < len(png):
            size = struct.unpack('>I', png[offset:offset+4])[0]
            kind, payload = png[offset+4:offset+8], png[offset+8:offset+8+size]
            crc = struct.unpack('>I', png[offset+8+size:offset+12+size])[0]
            self.assertEqual(crc, zlib.crc32(kind+payload) & 0xffffffff)
            if kind == b'IDAT':
                data += payload
            offset += 12+size
        rows = zlib.decompress(data)
        for x, y in ((0, 0), (15, 0), (0, 15), (15, 15)):
            start = y*65+1+x*4
            self.assertEqual(rows[start:start+4], bytes(COLORS[(y >= 8)*2+(x >= 8)]))

    def test_gltf_buffer_layout(self):
        model = make_gltf(.5)
        binary = base64.b64decode(model['buffers'][0]['uri'].split(',')[1])
        self.assertEqual(len(binary), model['buffers'][0]['byteLength'])
        for view in model['bufferViews']:
            self.assertEqual(view['byteOffset'] % 4, 0)
            self.assertLessEqual(view['byteOffset']+view['byteLength'], len(binary))
        self.assertIn('TANGENT', model['meshes'][0]['primitives'][0]['attributes'])

    def test_flat_normals_and_linear_roughness(self):
        _, guide = expected_regions(0, False)
        np.testing.assert_allclose(guide[:, :3], [[0, 0, -1]]*4)
        np.testing.assert_allclose(guide[:, 3], np.array([64, 128, 192, 224])/255*.7)

    def test_mirrored_tangent_only_flips_x(self):
        _, standard = expected_regions(.5, False)
        _, mirrored = expected_regions(.5, True)
        np.testing.assert_allclose(mirrored, standard*[-1, 1, 1, 1])
        np.testing.assert_allclose(np.linalg.norm(standard[:, :3], axis=1), 1)

    def test_base_color_is_srgb_decoded_before_factor(self):
        albedo, _ = expected_regions(.5, False)
        self.assertAlmostEqual(albedo[0, 0], ((64/255+.055)/1.055)**2.4*.8)
        self.assertEqual(albedo[0, 3], 1)
        self.assertGreater(np.linalg.norm(albedo[0]-albedo[1]), .1)

    def test_committed_fixture_references_and_reproducibility(self):
        root = ROOT/'Assets/Scenes/PathTracingValidation/input-textured'
        for name, scale in (('mapped', .5), ('flat', 0), ('mirrored', .5)):
            scene = json.loads((root/('scene.json' if name == 'mapped' else 'scene-'+name+'.json')).read_text())
            self.assertEqual(scene['nodes'][0]['assetId'], scene['assets'][0]['id'])
            self.assertEqual(scene['nodes'][0]['type'], 'gltf')
            self.assertTrue(all(value > 0 for value in scene['nodes'][0]['scale']))
            model = json.loads((root/scene['assets'][0]['path']).read_text())
            expected = make_gltf(scale)
            if name == 'mirrored':
                expected['nodes'][0]['scale'] = [-1, 1, 1]
            self.assertEqual(model, expected)
            self.assertTrue((root/scene['renderPreset']).is_file())


if __name__ == '__main__':
    unittest.main()
