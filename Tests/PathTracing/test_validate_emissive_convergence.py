import json
import unittest

from validate_emissive_convergence import ROOT, seed_variance, assess_curve


class EmissiveConvergenceTests(unittest.TestCase):
    def test_seed_variance_excludes_spatial_signal(self):
        self.assertEqual(seed_variance([[0, 100, 50]]*4), 0)
        self.assertAlmostEqual(seed_variance([[0, 1], [1, 2], [2, 3], [3, 4]]), 5/3)

    def test_inverse_sample_count_variance_passes(self):
        result = assess_curve([16, 64, 256], [1, .25, .0625])
        self.assertEqual(result['status'], 'passed')
        self.assertAlmostEqual(result['logVarianceSlope'], -1)

    def test_stagnant_noise_fails(self):
        self.assertEqual(assess_curve([16, 64, 256], [1, 1, 1])['status'], 'failed')

    def test_zero_noise_and_non_increasing_counts_rejected(self):
        with self.assertRaises(ValueError):
            assess_curve([16, 64, 256], [0, 0, 0])
        with self.assertRaises(ValueError):
            assess_curve([16, 16, 64], [1, .25, .0625])

    def test_archived_convergence_result(self):
        path = ROOT/'Assets/Scene/PathTracingValidation/01-emissive-nee-baseline/evaluation-convergence-results.json'
        result = json.loads(path.read_text())
        self.assertEqual(result['status'], 'passed')
        self.assertEqual(len(result['records']), 36)
        self.assertEqual(len(result['curves']), 3)
        self.assertTrue(result['repeat']['passed'])
        self.assertTrue(all(curve['status'] == 'passed' for curve in result['curves']))


if __name__ == '__main__':
    unittest.main()
