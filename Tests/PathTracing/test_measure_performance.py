import unittest
from unittest.mock import patch
import subprocess
from measure_performance import cases, percentile, parse_timing_log, make_fixture, validate_isolation
from measure_performance import other_renderers, HIDDEN_PROCESS
from assess_performance import assess
from summarize_performance import telemetry_summary


class PerformanceTests(unittest.TestCase):
    def test_isolation_helper_is_hidden_and_excludes_owned_process(self):
        result = subprocess.CompletedProcess([], 0, stdout='[{"Id":123,"Path":"owned.exe"},{"Id":456,"Path":"other.exe"}]')
        with patch("measure_performance.subprocess.run", return_value=result) as run:
            self.assertEqual(other_renderers(123), [dict(Id=456, Path="other.exe")])
            self.assertEqual(run.call_args.kwargs["creationflags"], HIDDEN_PROCESS)

    def test_other_renderer_rejected(self):
        validate_isolation([])
        with self.assertRaises(RuntimeError):
            validate_isolation([dict(Id=123, Path="other-work.exe")])

    def test_stability_and_incomplete_cohorts(self):
        report = dict(status="complete", failures=[], repeats=3, warmupObservations=64,
                      measurementObservations=64, cases=[dict(name="baseline", width=10, height=10,
                      samplesPerFrame=1)], runs=[dict(case="baseline", repeat=index, values=[1.0] * 64)
                      for index in range(3)])
        self.assertEqual(assess(report)["status"], "accepted")
        report["runs"][2]["values"] = [2.0] * 64
        result = assess(report)
        self.assertEqual(result["status"], "inconclusive")
        self.assertIsNone(result["cases"][0]["acceptedTimeRatioToBaseline"])
        report["status"] = "running"
        with self.assertRaises(ValueError):
            assess(report)

    def test_unstable_tail_rejected(self):
        report = dict(status="complete", failures=[], repeats=3, warmupObservations=64,
                      measurementObservations=64, cases=[dict(name="baseline", width=10, height=10,
                      samplesPerFrame=1)], runs=[dict(case="baseline", repeat=index,
                      values=[1.0] * 56 + [3.0] * 8) for index in range(3)])
        self.assertEqual(assess(report)["status"], "inconclusive")
    def test_one_factor_at_a_time(self):
        matrix = cases()
        for case in matrix[1:]:
            changed = {k for k in matrix[0] if k not in ("name", "axis") and case[k] != matrix[0][k]}
            expected = {"width", "height"} if case["axis"] == "resolution" else {"stacks", "slices"} if case["axis"] == "geometry" else {case["axis"]}
            self.assertEqual(changed, expected)

    def test_percentile_interpolates_and_rejects_bad_data(self):
        self.assertEqual(percentile([4, 1, 3, 2], .5), 2.5)
        self.assertAlmostEqual(percentile([4, 1, 3, 2], .95), 3.85)
        for values in ([], [0], [float("nan")]):
            with self.assertRaises(ValueError):
                percentile(values, .5)

    def test_capture_excluded_and_cpu_not_used(self):
        text = "[FPS] Frame 1: 200 FPS\n[GPU] Frame 1: total 5 ms\n[GPU Pass] PathTracingPass: 3.5 ms\n[PathTracing] {}\n[GPU] Frame 2: total 100 ms\n[GPU Pass] PathTracingPass: 90 ms"
        self.assertEqual(parse_timing_log(text), [dict(cpuLogFrame=1, gpuMs=3.5)])
        self.assertEqual(parse_timing_log("[FPS] Frame 1: 200 FPS"), [])

    def test_light_count_preserves_total_intensity(self):
        for case in cases():
            scene, preset = make_fixture(case)
            self.assertAlmostEqual(sum(l["intensity"] for l in preset["lighting"]["lights"]), 8)
            self.assertEqual(len(preset["lighting"]["lights"]), case["lights"])

    def test_invalid_gpu_observations_are_rejected(self):
        with self.assertRaises(ValueError):
            parse_timing_log("[GPU] Frame 1: total 1 ms\n[GPU Pass] PathTracingPass: nan ms")
        with self.assertRaises(ValueError):
            parse_timing_log("[GPU] Frame 1: total 1 ms\n[GPU Pass] PathTracingPass: 1 ms\n[GPU Pass] PathTracingPass: 1 ms")

    def test_gpu_telemetry_missing_values_remain_explicit(self):
        result = telemetry_summary([dict(exitCode=0, gpu="2026/10/02 11:00:00, P8, 50, 210 MHz, [N/A], 14 W")])
        self.assertEqual(result["smClockRangeMHz"], [210, 210])
        self.assertEqual(result["pStates"], ["P8"])
        self.assertIsNone(result["memoryClockRangeMHz"])
        self.assertIsNone(telemetry_summary([])["smClockRangeMHz"])


if __name__ == "__main__":
    unittest.main()
