import unittest

import numpy as np

from validate_volume_furnace import assess, summarize


class FurnaceTests(unittest.TestCase):
    def test_unit_control(self):
        self.assertEqual(assess(np.ones((128, 128, 4)), True)["mean"], [1, 1, 1])

    def test_dark_matched_control_rejected(self):
        with self.assertRaises(ValueError):
            assess(np.full((128, 128, 4), .9), True)

    def test_rough_loss_is_measurement(self):
        self.assertEqual(assess(np.full((128, 128, 4), .7), False)["minimum"], .7)

    def test_invalid_evidence(self):
        for value in (np.nan, np.inf, -1, 1.1):
            with self.subTest(value=value), self.assertRaises(ValueError):
                assess(np.full((128, 128, 4), value), False)
        with self.assertRaises(ValueError):
            assess(np.ones((16, 16, 4)), True)

    def test_budget_comparison(self):
        names = ["matched-smooth", "matched-rough", "smooth-b4", "smooth-b8", "smooth-b16",
                 "rough-b4", "rough-b8", "rough-b16"]
        cases = [dict(name=name, assessment=dict(mean=[1, 1, 1])) for name in names]
        self.assertEqual(summarize(cases)["smooth"]["delta8to16"], [0, 0, 0])
        with self.assertRaises(ValueError):
            summarize(cases[:-1])
        with self.assertRaises(ValueError):
            summarize(cases[:-1] + [cases[0]])
        cases[-1]["assessment"]["mean"] = [np.nan, 1, 1]
        with self.assertRaises(ValueError):
            summarize(cases)


if __name__ == "__main__":
    unittest.main()
