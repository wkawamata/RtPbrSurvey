import copy
import unittest

import numpy as np

from attribute_nrd_prepass_lobes import attribute, LONG_RESOURCES


class LobeAttributionTests(unittest.TestCase):
    def buffers(self, diffuse):
        buffers = {name: np.zeros((2, 3, 4), dtype=np.float32) for name in LONG_RESOURCES}
        for name in ('NrdDiffuseFactor', 'NrdSpecularFactor'):
            buffers[name][:] = 1
        buffers['NrdOutputDiffuse'][..., :3] = diffuse
        buffers['NrdOutputSpecular'][..., :3] = 1
        buffers['NrdInputAdmission'][:] = [1, 0, 0, 0]
        buffers['NrdCompositionDiagnostics'][:] = [1, 0, 0, 0]
        buffers['SceneColor'][:] = [2, 2, 2, 1]
        buffers['NrdSceneColor'][:] = [diffuse+1, diffuse+1, diffuse+1, 1]
        return buffers

    def test_diffuse_change_explains_total_without_claiming_ground_truth(self):
        result = attribute(self.buffers(1), self.buffers(3), np.ones((2, 3), dtype=bool))
        self.assertEqual(result['totalDeltaMeanRgb'], [2, 2, 2])
        self.assertEqual(result['components']['Diffuse']['deltaMeanRgb'], [2, 2, 2])
        self.assertEqual(result['components']['Specular']['maximumAbsoluteChange'], 0)
        self.assertEqual(result['maximumDeltaClosureError'], 0)
        self.assertFalse(result['perLobeGroundTruthAvailable'])

    def test_fallback_bad_composition_or_changed_native_is_rejected(self):
        control = self.buffers(1)
        for name in ('NrdCompositionDiagnostics', 'NrdSceneColor', 'SceneColor'):
            changed = copy.deepcopy(control)
            changed[name][0, 0, 0] += 1
            with self.assertRaises(ValueError):
                attribute(control, changed, np.ones((2, 3), dtype=bool))
        with self.assertRaises(ValueError):
            attribute(control, control, np.zeros((2, 3), dtype=bool))


if __name__ == '__main__':
    unittest.main()
