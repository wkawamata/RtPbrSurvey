import json
from pathlib import Path
import tempfile
import unittest

from validate_emissive_combined import ROOT, make_fixture, set_components, compare_rgb


class CombinedLightingTests(unittest.TestCase):
    def test_component_toggles_preserve_scene_and_light(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, preset = make_fixture(Path(directory))
            self.assertEqual(len(scene['assets']), 1)
            self.assertEqual(preset['lighting']['lights'][0]['type'], 'point')
            self.assertEqual(preset['lighting']['lights'][0]['color'], [.2, .5, 1])
            for component in ['emissive', 'direct', 'environment', 'combined', 'off']:
                set_components(preset, component, 2)
                settings = preset['pathTracing']
                self.assertEqual(settings['emissiveEnabled'], component in ['emissive', 'combined'])
                self.assertEqual(settings['directLightingEnabled'], component in ['direct', 'combined'])
                self.assertEqual(settings['environmentEnabled'], component in ['environment', 'combined'])
                self.assertEqual(settings['maxBounces'], 2)
                self.assertEqual(len(preset['lighting']['lights']), 1)

    def test_rgb_checks_detect_cancellation_hidden_in_mean(self):
        reference = [[1, 1, 1]]*4
        result = compare_rgb(reference, [[1.5, .5, 1]]*4)
        self.assertEqual(result['mean']['status'], 'passed')
        self.assertEqual(result['channels'][0]['status'], 'failed')
        self.assertEqual(result['channels'][1]['status'], 'failed')

    def test_rgb_shape_must_match(self):
        with self.assertRaises(ValueError):
            compare_rgb([[1, 1, 1]]*4, [[1, 1]]*4)

    def test_archived_fixture_has_relative_paths_and_all_sources(self):
        folder = ROOT/'Assets/Scene/PathTracingValidation/12-combined-lighting'
        scene = json.loads((folder/'scene.json').read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            generated, _ = make_fixture(Path(directory))
            self.assertEqual(scene['nodes'], generated['nodes'])
        self.assertTrue(scene['description'])
        preset = json.loads((folder/scene['renderPreset']).read_text())
        for field in ['emissiveEnabled', 'environmentEnabled', 'directLightingEnabled']:
            self.assertTrue(preset['pathTracing'][field])
        self.assertEqual(preset['pathTracing']['environmentSamplingMode'], 5)
        self.assertEqual(preset['pathTracing']['emissiveSamplingMode'], 2)
        for asset in scene['assets']:
            self.assertTrue((folder/asset['path']).is_file())
        result = json.loads((folder/'evaluation-results.json').read_text())
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(len(result['records']), 37)
        self.assertEqual(len(result['comparisons']), 6)
        self.assertEqual(result['offMaximum'], 0)


if __name__ == '__main__':
    unittest.main()
