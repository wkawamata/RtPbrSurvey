import unittest
from measure_rr_release import parse_log


class ReleaseTimingParserTests(unittest.TestCase):
    def log(self, reset=False):
        lines = []
        for frame in range(1, 6):
            lines.append(f"[PT Progress] frame={frame} wall={frame/10:.9f} sampleIndex={frame} rrCompleted={frame//3} rrResets={2 if reset and frame >= 3 else 1}")
            lines.append(f"[GPU] Frame {frame}: latest completed total {frame*10} ms")
            if frame % 2 == 0:
                lines.append(f"[GPU Pass] PT DLSS RR Native: {frame*10} ms")
        lines.append('[PathTracing] {"reconstructionOutput":"rr"}')
        lines.append('[GPU] Frame 6: latest completed total 999 ms')
        return '\n'.join(lines)

    def test_interval_and_active_passes_exclude_capture(self):
        result = parse_log(self.log(), 1, 3)
        self.assertEqual(result['gpuTotal']['count'], 3)
        self.assertEqual(result['gpuTotal']['meanMs'], 30)
        self.assertEqual(result['activePasses']['PT DLSS RR Native']['count'], 2)
        self.assertEqual(result['activePasses']['PT DLSS RR Native']['meanMs'], 30)
        self.assertAlmostEqual(result['freshRrOutputsPerSecond'], 5)

    def test_packet_latency_counts_completions_once(self):
        log = self.log()
        for frame in range(1, 6):
            packet = f"[RR Packet] completed={frame//3} frameId=9 inputPolls=2 outputPolls=2 inputWaitMs=12 admittedWaitMs=1 evaluateCpuMs=0.2 outputWaitMs=20 totalMs=33.3"
            log = log.replace(f"[GPU] Frame {frame}:", packet + f"\n[GPU] Frame {frame}:")
        result = parse_log(log, 1, 3)
        self.assertEqual(result['packetLatency']['count'], 1)
        self.assertEqual(result['packetLatency']['means']['inputWaitMs'], 12)
        self.assertEqual(result['packetLatency']['means']['totalMs'], 33.3)

    def test_nonfinite_packet_latency_rejected(self):
        with self.assertRaises(ValueError):
            parse_log('[RR Packet] completed=1 totalMs=nan\n' + self.log(), 1, 3)

    def test_history_reset_invalidates_interval(self):
        with self.assertRaises(ValueError):
            parse_log(self.log(reset=True), 1, 3)

    def test_missing_measurements_rejected(self):
        with self.assertRaises(ValueError):
            parse_log(self.log(), 1, 20)


if __name__ == '__main__':
    unittest.main()
