import unittest
from measure_performance import cases, percentile, parse_timing_log, make_fixture
from summarize_performance import telemetry_summary


class PerformanceTests(unittest.TestCase):
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
