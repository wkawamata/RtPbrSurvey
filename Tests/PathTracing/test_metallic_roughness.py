import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from validate_metallic_roughness import assess, bilinear_repeat, closest_hit, make_case, node_matrix, reference_mesh


class MetallicRoughnessTests(unittest.TestCase):
    def test_bilinear_texel_centers_and_repeat(self):
        pixels = np.array([[[0, 64, 255, 0], [255, 192, 0, 255]]], dtype=np.uint8)
        values = bilinear_repeat(pixels, [[.25, .5], [.75, .5], [1.25, .5], [-.25, .5], [.5, .5]])
        np.testing.assert_allclose(values[0], pixels[0, 0] / 255)
        np.testing.assert_allclose(values[1], pixels[0, 1] / 255)
        np.testing.assert_allclose(values[2], values[0])
        np.testing.assert_allclose(values[3], values[1])
        np.testing.assert_allclose(values[4], .5 * (values[0] + values[1]))

    def test_closest_front_face_and_barycentrics(self):
        triangles = np.array([[[-1, -1, 0], [0, 1, 0], [1, -1, 0]],
                              [[-1, -1, 2], [0, 1, 2], [1, -1, 2]]], dtype=float)
        index, bary, distance = closest_hit(triangles, np.array([0, 0, -5]), np.array([0, 0, 1]), .001, 100)
        self.assertEqual(index, 0)
        self.assertAlmostEqual(distance, 5)
        np.testing.assert_allclose(bary, [.25, .5, .25])
        self.assertIsNone(closest_hit(triangles[:, ::-1], np.array([0, 0, -5]), np.array([0, 0, 1]), .001, 100))

    def test_node_trs_and_matrix_agree(self):
        node = dict(translation=[1, 2, 3], scale=[2, 3, 4], rotation=[0, 0, np.sqrt(.5), np.sqrt(.5)])
        transform = node_matrix(node)
        np.testing.assert_allclose(transform @ [1, 0, 0, 1], [1, 4, 3, 1], atol=1e-12)
        np.testing.assert_allclose(node_matrix(dict(matrix=transform.reshape(-1, order="F").tolist())), transform)

    def test_reference_fixture_preserves_factors(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "case"
            _, asset = make_case(folder, "textured")
            triangles, uv, pbr, texture = reference_mesh(asset)
            self.assertEqual(triangles.shape, (2, 3, 3))
            self.assertEqual(uv.shape, (2, 3, 2))
            self.assertEqual(texture.shape, (2, 2, 4))
            self.assertEqual(pbr["metallicFactor"], .7)
            self.assertEqual(pbr["roughnessFactor"], .6)
            model = json.loads(asset.read_text(encoding="utf-8"))
            model["scenes"][0]["nodes"].append(0)
            asset.write_text(json.dumps(model), encoding="utf-8")
            with self.assertRaises(ValueError):
                reference_mesh(asset)

    def test_assessment_rejects_wrong_material_or_depth(self):
        with tempfile.TemporaryDirectory() as temporary:
            _, asset = make_case(Path(temporary) / "case", "factor-only")
            normal = np.zeros((160, 160, 4))
            normal[:, :, 2] = -1
            normal[:, :, 3] = .6
            material = np.zeros_like(normal)
            material[:, :, :3] = .04 * .3 + np.array([.3, .5, .7]) * .7
            material[:, :, 3] = .7
            depth = np.full((160, 160, 1), 5.0)
            meta = dict(width=160, height=160, cameraPosition=[0, 0, -5], rayTMin=.001, rayTMax=100,
                        inverseViewProjection=[[.5, 0, 0, 0], [0, .5, 0, 0], [0, 0, 10, -4], [0, 0, 0, 1]])
            captures = {"NormalRoughness": ({}, normal), "MaterialF0Metallic": ({}, material),
                        "ViewZ": ({"metadata": meta}, depth)}
            self.assertTrue(assess(asset, captures, "factor-only")["checks"]["passed"])
            for array, channel in ((normal, 3), (material, 3), (material, 0), (depth, 0)):
                saved = array[:, :, channel].copy()
                array[:, :, channel] = .1
                self.assertFalse(assess(asset, captures, "factor-only")["checks"]["passed"])
                array[:, :, channel] = saved
            normal[:, :, :3] = 0
            with self.assertRaises(ValueError):
                assess(asset, captures, "factor-only")


if __name__ == "__main__":
    unittest.main()
