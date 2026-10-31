import unittest

import numpy as np

from validate_nrd_runtime_reconstruction import assess_reconstruction


class RuntimeReconstructionTests(unittest.TestCase):
    def fixture(self):
        actual = np.array([[[1., 2, 3, 4], [0, 0, 0, 0]]])
        candidate = actual.copy()
        candidate[0, 0, 3] = 0
        states = np.array([[[0., 3, 0, 0], [0, 0, 0, 0]]])
        validity = np.array([[[1., 0, 1, 0], [0, 0, 0, 0]]])
        viewz = np.array([[[2., 0, 0, 0], [1e6, 0, 0, 0]]])
        return [actual, actual.copy(), candidate, states, validity, viewz]

    def test_resolved_and_unresolved(self):
        arrays = self.fixture()
        self.assertEqual(assess_reconstruction(*arrays)['reconstructed'], 1)
        arrays[0][0, 0, 3] = arrays[1][0, 0, 3] = 0
        self.assertEqual(assess_reconstruction(*arrays)['unresolved'], 1)

    def test_corruption(self):
        for index, pixel, channel, value in ((0, 0, 0, 2), (0, 0, 3, 5),
                (0, 1, 0, .1), (0, 0, 3, np.nan), (4, 0, 3, 1), (4, 0, 1, 1), (5, 0, 0, 1000)):
            with self.subTest(index=index, pixel=pixel):
                arrays = self.fixture()
                arrays[index][0, pixel, channel] = value
                with self.assertRaises(ValueError):
                    assess_reconstruction(*arrays)

    def test_shape(self):
        arrays = self.fixture()
        arrays[1] = arrays[1][:, :1]
        with self.assertRaises(ValueError):
            assess_reconstruction(*arrays)


if __name__ == '__main__':
    unittest.main()
