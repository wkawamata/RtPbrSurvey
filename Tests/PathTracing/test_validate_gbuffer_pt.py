import unittest
import numpy as np
from validate_gbuffer_pt import compare, interior
from validate_materials import expected_material


class GBufferComparisonTests(unittest.TestCase):
    def fixture(self):
        pt = {name: np.zeros((50, 100, channels)) for name, channels in
            [('NormalRoughness', 4), ('Albedo', 4), ('Emissive', 3)]}
        gb = {name: np.zeros((50, 100, 4)) for name in ['Normal', 'Albedo', 'PBRParams', 'Emissive']}
        for index in range(2):
            region = slice(index*50, (index+1)*50)
            for name in pt:
                pt[name][:, region] = expected_material(index, name)
            gb['Normal'][:, region, :3] = pt['NormalRoughness'][:, region, :3]
            gb['Albedo'][:, region] = np.round(pt['Albedo'][:, region]*255)/255
            gb['PBRParams'][:, region] = [0, round(.37*255)/255, 1, 1]
            gb['Emissive'][:, region, :3] = pt['Emissive'][:, region]
        return pt, gb

    def test_quantized_inputs_pass(self):
        self.assertTrue(all(c['passed'] for c in compare(*self.fixture())))

    def test_common_error_is_not_hidden_by_pair_comparison(self):
        pt, gb = self.fixture()
        pt['Albedo'][:, :, :3] += .1
        gb['Albedo'][:, :, :3] += .1
        self.assertFalse(all(c['passed'] for c in compare(pt, gb)))

    def test_no_visible_pixels_rejected(self):
        pt, gb = self.fixture()
        gb['PBRParams'][:, :, 3] = 0
        with self.assertRaises(ValueError):
            compare(pt, gb)

    def test_interior_does_not_wrap_edges(self):
        mask = interior(np.ones((10, 10), dtype=bool))
        self.assertEqual(mask.sum(), 36)


if __name__ == '__main__':
    unittest.main()
