"""Guard the diagnostic's geometry and lighting isolation controls."""
import json
from pathlib import Path
import tempfile
import unittest

from inspect_hybrid_ibl_visibility import prepare


class VisibilityFixtures(unittest.TestCase):
    def test_receiver_and_camera_are_identical_across_closure(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            prepare(root)
            def scene(state):
                return json.loads((root / "cornell" / state / "scene.json").read_text(encoding="utf-8"))
            opened, closed = scene("open-diffuse"), scene("closed-diffuse")
            self.assertEqual(opened["camera"], closed["camera"])
            self.assertEqual(opened["environment"], closed["environment"])
            self.assertEqual(opened["nodes"], closed["nodes"][:-1])
            self.assertEqual(closed["nodes"][-1]["translation"], [0, 0, -1.55])

    def test_lighting_and_path_controls(self):
        with tempfile.TemporaryDirectory() as folder:
            for name, label, mode, bounces, path in prepare(Path(folder)):
                settings = json.loads((path / "render-preset.json").read_text(encoding="utf-8"))
                lighting = settings["lighting"]
                self.assertFalse(lighting["directLightEnabled"])
                self.assertFalse(lighting["emissiveEnabled"])
                self.assertFalse(settings["hybridReflection"]["contributionEnabled"])
                self.assertEqual(lighting["diffuseIblEnabled"], mode in ("diffuse", "both", "pt"))
                self.assertEqual(lighting["specularIblEnabled"], mode in ("specular", "both", "pt"))
                if mode == "pt":
                    self.assertGreaterEqual(bounces, 2)
                    self.assertEqual(settings["pathTracing"]["maxBounces"], bounces)


if __name__ == "__main__":
    unittest.main()
