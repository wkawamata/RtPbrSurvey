import math
import json
from pathlib import Path
from types import SimpleNamespace
import struct
import tempfile
import unittest

from compare_hdr import build_capture_command, mean_image, read_pfm, rmse, write_direct_only_preset
from compare_convergence import metrics
from run_convergence_suite import reference_metrics


class HdrTests(unittest.TestCase):
    def test_orientation_roi_and_hdr_range(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.pfm"
            path.write_bytes(b"PF\n2 2\n-1.0\n" + struct.pack("<12f", *range(12)))
            dimensions, data = read_pfm(path, (1, 0, 1, 2))
            self.assertEqual(dimensions, (2, 2))
            self.assertEqual(list(data), [9, 10, 11, 3, 4, 5])
            with self.assertRaises(ValueError):
                read_pfm(path, (2, 0, 1, 1))

    def test_nonfinite_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test.pfm"
            path.write_bytes(b"PF\n1 1\n-1.0\n" + struct.pack("<3f", 1, math.nan, 3))
            with self.assertRaises(ValueError):
                read_pfm(path, (0, 0, 1, 1))

    def test_pfm_is_already_sample_normalized(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "normalized.pfm"
            path.write_bytes(b"PF\n1 1\n-1.0\n" + struct.pack("<3f", 2.5, 0.5, 4.0))
            _, values = read_pfm(path, (0, 0, 1, 1))
            self.assertEqual(list(values), [2.5, 0.5, 4.0])

    def test_metrics(self):
        self.assertEqual(mean_image([[1, 3, 5], [3, 5, 7]]), [2, 4, 6])
        self.assertEqual(rmse([1, 2, 3], [3, 4, 5]), 2)
        self.assertEqual(rmse([1, 2, 3], [1, 2, 3]), 0)
        with self.assertRaises(ValueError):
            rmse([1], [1, 2])

    def test_capture_command_for_scene_file(self):
        args = SimpleNamespace(exe=Path("app.exe"), scene="ignored",
                               scene_file=Path("scene.json"), render_preset=Path("preset.json"))
        command = build_capture_command(args, 0, 7, 32, Path("result.pfm"), Path("run.log"))
        self.assertEqual(command[:5], ["app.exe", "-SceneFile", "scene.json", "-RenderPreset", "preset.json"])
        self.assertEqual(command[command.index("-PathTracingSamples") + 1], "32")
        self.assertEqual(command[command.index("-PathTracingSeed") + 1], "7")
        args.scene_file = None
        args.render_preset = None
        command = build_capture_command(args, 7, 1, 64, Path("result.pfm"), Path("run.log"))
        self.assertEqual(command[:4], ["app.exe", "-AutoSelectGltfAsset", "ignored", "-UseSceneDefaults"])

    def test_direct_only_preset_preserves_lights(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.json"
            destination = Path(folder) / "direct-only.json"
            source.write_text(json.dumps({"lighting": {"lights": [{"id": 2}], "skyboxEnabled": True}}),
                              encoding="utf-8")
            write_direct_only_preset(source, destination)
            preset = json.loads(destination.read_text(encoding="utf-8"))
            self.assertEqual(preset["lighting"]["lights"], [{"id": 2}])
            self.assertFalse(preset["pathTracing"]["environmentEnabled"])
            self.assertFalse(preset["lighting"]["skyboxEnabled"])
            self.assertEqual(preset["pathTracing"]["maxBounces"], 1)

    def test_shared_reference_metrics(self):
        result = metrics([[1.0, 3.0], [3.0, 5.0]], [2.0, 4.0])
        self.assertEqual(result["meanImageRmse"], 0.0)
        self.assertEqual(result["seedVariance"], 2.0)

    def test_rgb_statistics_and_reference_standard_error(self):
        images = [[1, 2, 3, 3, 4, 5], [3, 4, 5, 5, 6, 7]]
        result = metrics(images, [2, 3, 4, 4, 5, 6])
        self.assertEqual(result["meanRgb"], [3, 4, 5])
        self.assertEqual(result["seedCount"], 2)
        self.assertEqual(result["perSeedRmse"], [1, 1])
        reference = reference_metrics(images)
        self.assertEqual(reference["meanPixelStandardError"], 1)
        self.assertEqual(reference["pairwiseDisagreementRmse"], [2])

    def test_statistics_reject_insufficient_or_mismatched_seeds(self):
        for images, reference in [([[1, 2, 3]], [1, 2, 3]), ([[1], [1, 2]], [1]), ([[], []], [])]:
            with self.assertRaises(ValueError):
                metrics(images, reference)


if __name__ == "__main__":
    unittest.main()
