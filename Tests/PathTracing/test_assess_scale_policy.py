import copy
import unittest

from assess_scale_policy import assess


def complete_report():
    scales = (.01, 1., 100.)
    return dict(status="complete", failures=[], visibility=[
        dict(scale=s, angle=a, policy="relative", passed=True) for s in scales for a in ("front", "angled")],
        contact=[dict(scale=s, policy="relative", shadowColumnCount=3) for s in scales],
        selfIntersection=[dict(scale=s, policy="relative", meanRatio=1, darkenedChannelFraction=0) for s in scales])


class ScalePolicyTests(unittest.TestCase):
    def test_complete_relative_policy_passes(self):
        self.assertEqual(assess(complete_report())["status"], "passed")

    def test_fixed_negative_control_does_not_reject_relative_policy(self):
        report = complete_report()
        report["visibility"].append(dict(scale=.01, angle="angled", policy="fixed", passed=False))
        result = assess(report)
        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(result["fixedVisibilityFailures"]), 1)

    def test_missing_or_rejected_required_measurements_fail(self):
        for field in ("visibility", "contact", "selfIntersection"):
            report = copy.deepcopy(complete_report())
            report[field].pop()
            self.assertEqual(assess(report)["status"], "failed")

    def test_nonfinite_self_hit_rejected(self):
        report = complete_report()
        report["selfIntersection"][0]["meanRatio"] = float("nan")
        self.assertEqual(assess(report)["status"], "failed")

    def test_capture_failure_rejected(self):
        report = complete_report()
        report["status"] = "running"
        self.assertEqual(assess(report)["status"], "failed")


if __name__ == "__main__":
    unittest.main()
