import json
from pathlib import Path
import tempfile
import unittest

from validate_emissive_rr import ROOT, make_fixture, deep_contribution


class DeepEmissiveTests(unittest.TestCase):
    def test_closed_room_has_only_emissive_lighting(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, preset = make_fixture(Path(directory))
            self.assertEqual(len(scene['nodes']), 7)
            self.assertEqual(len(scene['assets']), 1)
            self.assertEqual(preset['lighting']['lights'], [])
            self.assertFalse(preset['pathTracing']['environmentEnabled'])
            self.assertFalse(preset['pathTracing']['directLightingEnabled'])
            self.assertTrue(preset['pathTracing']['emissiveEnabled'])
            self.assertTrue(preset['shadow']['enabled'])
            self.assertEqual(preset['pathTracing']['maxBounces'], 8)

    def test_deep_contribution_requires_significant_positive_signal(self):
        self.assertTrue(deep_contribution([1]*4, [1.2]*4)['passed'])
        self.assertFalse(deep_contribution([1]*4, [1]*4)['passed'])
        self.assertFalse(deep_contribution([1]*4, [1.1, .9, 1.1, .9])['passed'])

    def test_deep_contribution_rejects_short_cohort(self):
        with self.assertRaises(ValueError):
            deep_contribution([1]*3, [2]*3)

    def test_archived_room_and_relative_paths(self):
        folder = ROOT/'Assets/Scene/PathTracingValidation/11-deep-emissive-rr'
        archived = json.loads((folder/'scene.json').read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            scene, _ = make_fixture(Path(directory))
            self.assertEqual(archived['nodes'], scene['nodes'])
            self.assertEqual(archived['materials'], scene['materials'])
        self.assertTrue(archived['description'])
        preset = json.loads((folder/archived['renderPreset']).read_text())
        self.assertEqual(preset['pathTracing']['emissiveSamplingMode'], 2)
        self.assertTrue(preset['pathTracing']['russianRouletteEnabled'])
        self.assertEqual(preset['pathTracing']['maxBounces'], 8)
        for asset in archived['assets']:
            self.assertTrue((folder/asset['path']).is_file())
        result = json.loads((folder/'evaluation-results.json').read_text())
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(len(result['records']), 29)
        self.assertTrue(result['deepContribution']['passed'])
        self.assertEqual(result['offMaximum'], 0)


if __name__ == '__main__':
    unittest.main()
