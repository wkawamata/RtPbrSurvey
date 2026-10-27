import unittest

import numpy as np

from validate_nrd_runtime_admission import expected_admission, assess_admission


class RuntimeAdmissionTests(unittest.TestCase):
    def fixture(self):
        signal = np.array([[[1., 2, 3, 4], [0, 0, 0, 5], [1, 2, 3, 0], [0, 0, 0, 0]]])
        specular = np.array([[[1., 2, 3, 4]]*4])
        states = np.array([[[4., 1, 4, 0], [0, 4, 0, 0], [0, 3, 0, 0], [0, 0, 0, 0]]])
        specular_states = np.array([[[4., 1, 4, 0]]*4])
        validity = np.array([[[1., 1, 0, 0]]*3+[[0, 0, 0, 0]]])
        depth = np.array([[[2., 0, 0, 0]]*4])
        return [signal, specular, states, specular_states, validity, depth]

    def test_admission_and_whole_pixel_rejection(self):
        inputs = self.fixture()
        outputs = expected_admission(*inputs)
        self.assertEqual(list(outputs[3][0, :, 1]), [0, 0, 4, 1])
        self.assertTrue((outputs[0][0, 1] == 0).all())
        self.assertTrue((outputs[1][0, 2:] == 0).all())
        self.assertEqual(outputs[2][0, 2, 0], 1e6)
        self.assertEqual(assess_admission(inputs, outputs)['zeroedInactiveDistances'], 1)

    def test_invalid_signal_and_guides(self):
        for index, pixel, channel, value, code in ((0, 0, 0, np.nan, 3), (0, 1, 0, 1, 3),
                (0, 0, 3, 1e20, 3), (2, 0, 1, 0, 3), (2, 0, 0, 0, 3),
                (4, 0, 1, 0, 2), (5, 0, 0, np.inf, 2)):
            inputs = self.fixture()
            inputs[index][0, pixel, channel] = value
            outputs = expected_admission(*inputs)
            self.assertEqual(outputs[3][0, pixel, 1], code)
            self.assertTrue((outputs[0][0, pixel] == 0).all())
            self.assertTrue((outputs[1][0, pixel] == 0).all())

    def test_distance_ceiling_and_output_corruption(self):
        inputs = self.fixture()
        inputs[0][0, 0, 3] = 65504.02
        outputs = expected_admission(*inputs)
        self.assertEqual(outputs[0][0, 0, 3], 65504)
        for index in range(4):
            corrupted = [x.copy() for x in outputs]
            corrupted[index][0, 0, 0] += .1
            with self.assertRaises(ValueError):
                assess_admission(inputs, corrupted)


if __name__ == '__main__':
    unittest.main()
