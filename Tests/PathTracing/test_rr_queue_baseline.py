"""CPU-only parser checks for RRQ-01; no GPU application is launched."""
import json
import math
import unittest
from measure_rr_queue_baseline import parse, stats


def fixture(skip=None, regress=False):
    lines=[]
    for frame in range(1,14):
        if frame == skip:
            continue
        completed=0 if regress and frame == 8 else frame//4
        lines.extend([
            f'[FPS] Frame {frame}: 100 FPS (10.00 ms)',
            f'[PT Progress] frame={frame} wall={frame*.01:.5f}',
            f'[RR Packet] frameId={frame//4} inputWaitMs=2 admittedWaitMs=1 evaluateCpuMs=0.1 outputWaitMs=3 totalMs=6',
            '[RenderGraphFrame] '+json.dumps(dict(gpuPacket=(frame+1)//2, rrCompleted=completed, rrPhase='pending')),
            f'[GPU] Frame {frame}: latest completed total 5.000 ms',
            '[GPU Pass] PT DLSS RR Native: 2.500 ms'])
    return '\n'.join(lines)


class QueueBaselineParserTests(unittest.TestCase):
    def test_rates_and_duplicate_packets(self):
        result=parse(fixture(),3,10)
        self.assertEqual(result['completed'],3)
        self.assertAlmostEqual(result['freshRrFps'],30)
        self.assertAlmostEqual(result['displayFps'],100)
        self.assertEqual(result['completedGpuPacketMs']['count'],6)
        self.assertEqual(result['gpuPassMs']['PT DLSS RR Native']['count'],6)
        self.assertEqual(result['packetLatencyMs']['totalMs']['count'],3)
        self.assertEqual(result['cpuMs']['mean'],10)

    def test_incomplete_interval_rejected(self):
        with self.assertRaises(ValueError):
            parse(fixture(skip=7),3,10)

    def test_counter_regression_rejected(self):
        with self.assertRaises(ValueError):
            parse(fixture(regress=True),3,10)

    def test_bad_timings_rejected(self):
        for value in [-1,math.nan,math.inf]:
            with self.assertRaises(ValueError):
                stats([value])


if __name__ == '__main__':
    unittest.main()
