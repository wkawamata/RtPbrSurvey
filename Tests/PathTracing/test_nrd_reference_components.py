import unittest

import numpy as np

from validate_nrd_reference_components import closure, COMPONENTS


class ReferenceComponentTests(unittest.TestCase):
    def buffers(self):
        buffers = {name: np.ones((2, 3, 4), dtype=np.float32) for name in COMPONENTS}
        buffers['total'] = np.ones((2, 3, 4), dtype=np.float32)
        buffers['total'][..., :3] = 4
        return buffers

    def test_component_energy_closure_is_not_reference_convergence(self):
        result = closure(self.buffers())
        self.assertEqual(result['maximumAbsoluteError'], 0)
        self.assertFalse(result['convergedReferenceQualified'])

    def test_missing_wrong_component_or_invalid_pixels_are_rejected(self):
        for mutation in ('missing', 'wrong', 'nan', 'alpha'):
            buffers = self.buffers()
            if mutation == 'missing':
                buffers.pop('diffuse')
            elif mutation == 'wrong':
                buffers['diffuse'][..., :3] *= .9
            elif mutation == 'nan':
                buffers['diffuse'][0, 0, 0] = np.nan
            else:
                buffers['diffuse'][0, 0, 3] = 0
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                closure(buffers)


if __name__ == '__main__':
    unittest.main()
