import unittest

import numpy as np

from validate_history import CASES, CHANGES
from validate_nrd_runtime_history import assess_history, ZERO_CASES


class RuntimeHistoryTests(unittest.TestCase):
    def fixture(self):
        images = {name: np.ones((1, 2, 4), dtype=np.float32) for name in CASES}
        for name in ZERO_CASES:
            images[name][:] = 0
        for name in ('resumed-32', 'fresh-32'):
            images[name][:] = 2
        for index, change in enumerate(CHANGES):
            shape = (2, 3, 4) if change == 'resize' else (1, 2, 4)
            for suffix in ('changed-16', 'fresh-16'):
                images[change+'-'+suffix] = np.full(shape, index+3, dtype=np.float32)
        return images

    def test_valid(self):
        self.assertEqual(len(assess_history(self.fixture())['exactComparisons']), 8)

    def test_reject_corrupt_or_incomplete(self):
        images = self.fixture()
        del images['paused-16']
        with self.assertRaises(ValueError):
            assess_history(images)
        for name in ('paused-16', 'reset-paused-0', 'batch-4-32', 'resize-fresh-16'):
            images = self.fixture()
            images[name][0, 0, 0] += .1
            with self.assertRaises(ValueError):
                assess_history(images)

    def test_zero_mutation_pair_is_not_success(self):
        for change in CHANGES:
            images = self.fixture()
            images[change+'-changed-16'][:] = images[change+'-fresh-16'][:] = 0
            with self.assertRaises(ValueError):
                assess_history(images)

    def test_unchanged_mutation_pair_is_not_success(self):
        previous = 'baseline-16'
        for change in CHANGES[:-1]:
            images = self.fixture()
            for suffix in ('changed-16', 'fresh-16'):
                images[change+'-'+suffix] = images[previous].copy()
            with self.assertRaises(ValueError):
                assess_history(images)
            previous = change+'-fresh-16'

    def test_tiles_reject_nonzero_or_nonfinite(self):
        images = {name: np.zeros_like(image[..., :1]) for name, image in self.fixture().items()}
        assess_history(images, tiles=True)
        for value in (1, np.nan):
            images['baseline-16'][0, 0, 0] = value
            with self.assertRaises(ValueError):
                assess_history(images, tiles=True)


if __name__ == '__main__':
    unittest.main()
