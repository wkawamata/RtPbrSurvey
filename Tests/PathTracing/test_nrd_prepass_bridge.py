import unittest

import numpy as np

from validate_nrd_prepass_bridge import compare, check_prepass, check_runtime, inventory, SEEDS, FRAMES


class PrepassBridgeTests(unittest.TestCase):
    def test_exact_comparison_does_not_hide_small_differences(self):
        image = np.ones((2, 3, 4), dtype=np.float32)
        self.assertTrue(compare(image, image)['equal'])
        changed = image.copy()
        changed[0, 0, 0] = np.nextafter(np.float32(1), np.float32(2))
        result = compare(image, changed)
        self.assertFalse(result['equal'])
        self.assertEqual(result['changedValues'], 1)
        for bad in (np.zeros((3, 2, 4)), image*np.nan):
            with self.assertRaises(ValueError):
                compare(image, bad)

    def test_only_declared_applied_control_metadata_is_removed(self):
        values = dict(profile='control', applied=True, diffuseRadius=30, specularRadius=50)
        self.assertEqual(check_prepass(dict(other=17, nrdPrepass=values)), dict(other=17))
        for key, value in (('applied', False), ('profile', 'specular-10'), ('specularRadius', 10)):
            with self.assertRaises(ValueError):
                check_prepass(dict(nrdPrepass=dict(values, **{key: value})))
        with self.assertRaises(ValueError):
            check_prepass({})

    def test_inventory_rejects_duplicates_and_missing_cases(self):
        rows = [dict(seed=seed, frames=frames, resource=name) for seed in SEEDS for frames in FRAMES
            for name in ('SceneColor', 'NrdSceneColor')]
        inventory(rows, ('SceneColor', 'NrdSceneColor'))
        for bad in (rows[:-1], rows[:-1]+[rows[0]], rows+[rows[0]]):
            with self.assertRaises(ValueError):
                inventory(bad, ('SceneColor', 'NrdSceneColor'))

    def test_runtime_only_excludes_validated_path_and_measured_performance(self):
        old = dict(capturePath='old', gpuTimeMs=1, primarySamplesPerSecond=10, maxBounces=4)
        new = dict(old, capturePath='new', gpuTimeMs=2, primarySamplesPerSecond=5)
        check_runtime(old, new, 'new')
        for changes in (dict(capturePath='wrong'), dict(gpuTimeMs=float('nan')),
                        dict(primarySamplesPerSecond=-1), dict(maxBounces=2), dict(unexpected=1)):
            with self.assertRaises(ValueError):
                check_runtime(old, dict(new, **changes), 'new')


if __name__ == '__main__':
    unittest.main()
