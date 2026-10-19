import unittest
import json
import numpy as np
from validate_emissive import rectangle_integral, reference, fixture, blocked_scene
from pathlib import Path
import tempfile


class EmissiveReferenceTests(unittest.TestCase):
    def test_quadrature_converges_and_channels_follow_emission(self):
        low = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 32)
        high = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8, 64)
        np.testing.assert_allclose(low, high, rtol=1e-10)
        np.testing.assert_allclose(high/high[2], [4, 2, 1])
        self.assertGreater(high.min(), 0)

    def test_increased_distance_reduces_radiance(self):
        close = rectangle_integral([0, 0, 0], [0, 1, 0], .5, .8)
        far = rectangle_integral([0, -3, 0], [0, 1, 0], .5, .8)
        self.assertTrue(np.all(far < close))

    def test_fixture_reference_is_finite(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, preset = fixture(Path(directory))
            value = reference(scene)
            self.assertGreater(value['mean'], 0)
            self.assertLess(value['quadratureRelativeChange'], .002)
            self.assertFalse(preset['pathTracing']['environmentEnabled'])

    def test_blocker_does_not_mutate_original_fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            scene, _ = fixture(Path(directory))
            blocked = blocked_scene(scene)
            self.assertEqual(len(blocked['nodes']), len(scene['nodes']) + 1)
            self.assertEqual(blocked['nodes'][-1]['translation'], [0, 1.5, 0])

    def test_archived_baseline_matches_generated_fixture(self):
        root = Path(__file__).resolve().parents[2]
        folder = root / 'Assets/Scene/PathTracingValidation/01-emissive-nee-baseline'
        archived = json.loads((folder / 'scene.json').read_text(encoding='utf-8'))
        preset = json.loads((folder / 'render-preset.json').read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            generated, _ = fixture(Path(directory))
            self.assertEqual(archived['nodes'], generated['nodes'])
            self.assertEqual(archived['camera'], generated['camera'])
            self.assertEqual(json.loads((folder / 'emitter.gltf').read_text()),
                json.loads((Path(directory) / 'emitter.gltf').read_text()))
        self.assertTrue(archived['description'])
        self.assertEqual(preset['renderingPath'], 2)
        self.assertTrue(preset['pathTracing']['emissiveEnabled'])
        self.assertEqual(preset['pathTracing']['emissiveSamplingMode'], 1)


if __name__ == '__main__':
    unittest.main()
