import copy
import unittest
from compare_volume_furnace import compare


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        names = ["matched-smooth", "matched-rough", "smooth-b4", "smooth-b8", "smooth-b16",
                 "rough-b4", "rough-b8", "rough-b16"]
        self.a = dict(status="passed-controls-and-measured-finite-budgets", frames=128,
            hashes=dict(executable="a", shader="b"), windowSize=[256, 256],
            cases=[dict(name=n, ior=1.5, roughness=.7, bounces=16,
                assessment=dict(mean=[1, 1, 1], roi=[1, 2, 3, 4]), capture=dict(inputs={"scene": "c"})) for n in names])
        self.b = copy.deepcopy(self.a)
        self.b["frames"] = 256

    def test_matching(self):
        self.assertEqual(len(compare(self.a, self.b)["cases"]), 8)

    def test_changed_inputs(self):
        self.b["cases"][0]["capture"]["inputs"]["scene"] = "changed"
        with self.assertRaises(ValueError):
            compare(self.a, self.b)

    def test_duplicate(self):
        self.b["cases"][-1] = self.b["cases"][0]
        with self.assertRaises(ValueError):
            compare(self.a, self.b)


if __name__ == "__main__":
    unittest.main()
