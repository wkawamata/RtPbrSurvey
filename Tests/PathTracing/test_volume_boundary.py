import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from validate_volume_boundary import assess, reference_rays
from volume_boundary_fixture import write_volume_fixture


class VolumeBoundaryTests(unittest.TestCase):
    def test_counts_use_ray_length_not_z_thickness(self):
        scene = dict(camera=dict(position=[2, 0, -5], target=[0, 0, 0], up=[0, 1, 0]))
        _, rays, roi = reference_rays(scene, 256, 256)
        image = np.zeros((256, 256, 4))
        image[roi][..., :2] = 1
        image[roi][..., 2] = 1/rays[..., 2]
        json.dumps(assess(image, scene, "slab-counts", 1))
        image[roi][..., 2] = 1
        with self.assertRaises(ValueError):
            assess(image, scene, "slab-counts", 1)

    def test_missing_exit_evidence_is_rejected(self):
        scene = dict(camera=dict(position=[2, 0, -5], target=[0, 0, 0], up=[0, 1, 0]))
        for name in ("entry-direction", "exit-position"):
            with self.assertRaises(ValueError):
                assess(np.zeros((256, 256, 4)), scene, name, 1.5)
        with self.assertRaises(ValueError):
            assess(np.full((256, 256, 4), np.nan), scene, "slab-status", 1)

    def test_budget_diagnostic_is_not_success(self):
        scene = dict(camera=dict(position=[2, 0, -5], target=[0, 0, 0], up=[0, 1, 0]))
        image = np.zeros((256, 256, 4))
        image[..., :3] = [3, 1, 0]
        assess(image, scene, "budget-control", 1)
        with self.assertRaises(ValueError):
            assess(image, scene, "slab-status", 1)

    def test_fixture_has_no_hidden_thin_material(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)/"fixture"
            scene, preset = write_volume_fixture(folder)
            gltf = json.loads((folder/"panes.gltf").read_text(encoding="utf-8"))
            self.assertEqual(len(gltf["materials"]), 1)
            self.assertNotIn("KHR_materials_transmission", gltf["materials"][0]["extensions"])
            self.assertEqual(scene["materials"][0]["transmission"]["mode"], "volume")
            self.assertEqual(scene["nodes"][-1]["primitive"]["kind"], "cube")
            self.assertEqual(preset["pathTracing"]["maxBounces"], 16)

    def test_tir_requires_significant_internal_reflection(self):
        scene = dict(camera=dict(position=[3, 0, -5], target=[0, 0, 2.68], up=[0, 1, 0]))
        with self.assertRaises(ValueError):
            assess(np.zeros((256, 256, 4)), scene, "tir-state", 1.5)


if __name__ == "__main__":
    unittest.main()
