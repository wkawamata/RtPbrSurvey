import unittest

import numpy as np

from probe_nrd_multispp_gate import assess


class MultiSppGateTests(unittest.TestCase):
    def buffers(self):
        image = np.ones((2, 3, 4), dtype=np.float32)
        return dict(SceneColor=image, NrdSceneColor=image.copy(), NrdValidity=np.zeros_like(image))

    def test_fallback_is_not_quality_evidence(self):
        result = assess(self.buffers())
        self.assertTrue(result['nativeFallbackExact'])
        self.assertFalse(result['nrdQualityEvaluated'])

    def test_unexpected_admission_or_output_change_is_rejected(self):
        for name in ('NrdValidity', 'NrdSceneColor'):
            buffers = self.buffers()
            buffers[name][0, 0, 0] += 1
            with self.assertRaises(ValueError):
                assess(buffers)


if __name__ == '__main__':
    unittest.main()
