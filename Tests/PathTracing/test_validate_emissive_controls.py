import json
import tempfile
from pathlib import Path
import unittest

from validate_emissive_controls import ROOT, make_fixture


class EmissiveControlsTests(unittest.TestCase):
    def fixture(self, case):
        with tempfile.TemporaryDirectory() as folder:
            return make_fixture(Path(folder), case)

    def test_backface_retains_height(self):
        scene, _ = self.fixture('backface')
        node = scene['nodes'][1]
        self.assertEqual(node['rotation'], [1, 0, 0, 0])
        self.assertEqual(node['translation'][1]-3, 3)

    def test_blocker_beyond_light(self):
        scene, _ = self.fixture('beyond')
        self.assertGreater(scene['nodes'][-1]['translation'][1]-.05, 3)

    def test_empty_uses_nonzero_environment(self):
        scene, preset = self.fixture('empty')
        self.assertEqual(scene['assets'], [])
        self.assertEqual(len(scene['nodes']), 1)
        self.assertTrue(preset['pathTracing']['environmentEnabled'])
        self.assertEqual(preset['pathTracing']['environmentSamplingMode'], 1)

    def test_shadows_disabled(self):
        _, preset = self.fixture('shadow-off')
        self.assertFalse(preset['shadow']['enabled'])

    def test_archived_paths_and_descriptions(self):
        root = ROOT/'Assets/Scene/PathTracingValidation'
        for pattern in ['05-*', '06-*', '07-*', '08-*']:
            folders = list(root.glob(pattern))
            self.assertEqual(len(folders), 1)
            folder = folders[0]
            scene = json.loads((folder/'scene.json').read_text(encoding='utf-8'))
            self.assertTrue(scene['description'])
            self.assertTrue((folder/scene['renderPreset']).is_file())
            for asset in scene['assets']:
                self.assertTrue((folder/asset['path']).is_file())


if __name__ == '__main__':
    unittest.main()
