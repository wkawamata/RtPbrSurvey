import unittest
import json
from pathlib import Path
import tempfile
import numpy as np
from validate_geometry import expected_normal, surface_frame, fixtures


class SurfaceFrameTests(unittest.TestCase):
    def test_orthonormal_frame(self):
        for mirrored in (False, True):
            for angle in (0, 30, -15):
                n, t, b = surface_frame(mirrored, angle)
                np.testing.assert_allclose(np.array([n, t, b]) @ np.array([n, t, b]).T, np.eye(3), atol=1e-12)

    def test_normal_map_changes_normal(self):
        self.assertGreater(np.linalg.norm(expected_normal(True)-expected_normal(False)), .3)

    def test_rotation_changes_normal(self):
        self.assertGreater(np.linalg.norm(expected_normal(False, angle=30)-expected_normal(False, angle=-15)), .5)

    def test_mirroring_changes_frame_handedness(self):
        self.assertLess(np.linalg.det(np.array(surface_frame(False))), 0)
        self.assertGreater(np.linalg.det(np.array(surface_frame(True))), 0)

    def test_normal_scale_changes_normal(self):
        self.assertGreater(np.linalg.norm(expected_normal(True)-expected_normal(True, normal_scale=.6)), .1)

    def test_separate_meshes_have_distinct_assets_and_materials(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            scene_path, _, _ = fixtures(output, 'separate', True, False, True, True)
            scene = json.loads(scene_path.read_text())
            self.assertNotEqual(scene['nodes'][0]['assetId'], scene['nodes'][1]['assetId'])
            assets = [json.loads((output/asset['path']).read_text()) for asset in scene['assets']]
            self.assertEqual(assets[0]['nodes'][0]['scale'][0], -assets[1]['nodes'][0]['scale'][0])
            self.assertEqual(assets[1]['materials'][0]['normalTexture']['scale'], .6)
            self.assertEqual(assets[1]['materials'][0]['pbrMetallicRoughness']['roughnessFactor'], .71)


if __name__ == '__main__':
    unittest.main()
