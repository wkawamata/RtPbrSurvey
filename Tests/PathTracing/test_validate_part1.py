"""Independent attenuation reference and editable-fixture contracts."""
import json
from pathlib import Path
import unittest

from validate_part1 import falloff


class Part1Tests(unittest.TestCase):
    def test_inverse_square_limit(self):
        self.assertAlmostEqual(falloff(2, 1e9) / falloff(4, 1e9), 4)

    def test_range_excludes_light(self):
        self.assertEqual(falloff(20, 20), 0)
        self.assertEqual(falloff(21, 20), 0)

    def test_range_window(self):
        self.assertAlmostEqual(falloff(2, 4), (15 / 16) ** 2 / 4)

    def test_fixture_references_and_radiance(self):
        root = Path(__file__).resolve().parents[2] / "Assets/Scenes/PathTracingValidation"
        plan = json.loads((root / "validation-plan.json").read_text())
        for scene_id in plan["scenes"]:
            directory = root / scene_id
            scene = json.loads((directory / "scene.json").read_text())
            preset = json.loads((directory / scene["renderPreset"]).read_text())
            self.assertEqual(scene["assets"], [])
            self.assertEqual(scene["schemaVersion"], 1)
            self.assertEqual(preset["pathTracing"]["debugOutput"], 3)
            self.assertFalse(preset["pathTracing"]["russianRouletteEnabled"])
            materials = {m["id"] for m in scene["materials"]}
            self.assertEqual(len({n["id"] for n in scene["nodes"]}), len(scene["nodes"]))
            for node in scene["nodes"]:
                self.assertIn(node["materialId"], materials)
                self.assertEqual(node["type"], "primitive")


if __name__ == "__main__":
    unittest.main()
