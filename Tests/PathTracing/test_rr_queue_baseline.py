"""CPU-only parser checks for RRQ-01; no GPU application is launched."""
import json
import math
import unittest
from measure_rr_queue_baseline import parse, stats


def fixture(skip=None, regress=False, cpu=False):
    lines=[]
    for frame in range(1,14):
        if frame == skip:
            continue
        completed=0 if regress and frame == 8 else frame//4
        observation = dict(gpuPacket=(frame+1)//2, rrCompleted=completed, rrPhase='pending',
                           rrPublished=completed, rrRetainedAgeMs=frame%4)
        if cpu:
            observation.update(rendererCpuMs=10, rendererCpuPhaseMs={
                'update': 1, 'record': 2, 'gpuWait': 6, 'present': .5, 'bounds': .5})
        lines.extend([
            f'[FPS] Frame {frame}: 100 FPS (10.00 ms)',
            f'[PT Progress] frame={frame} wall={frame*.01:.5f}',
            f'[RR Packet] frameId={frame//4} inputWaitMs=2 admittedWaitMs=1 evaluateCpuMs=0.1 outputWaitMs=3 totalMs=6',
            '[RenderGraphFrame] '+json.dumps(observation),
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
        self.assertEqual(result['retainedAgeMs']['count'],10)
        self.assertAlmostEqual(result['retainedAgeMs']['mean'],1.3)

    def test_cpu_phases_and_cost_per_verified_output(self):
        result = parse(fixture(cpu=True), 3, 10)
        self.assertEqual(result['rendererCpuMs']['mean'], 10)
        self.assertEqual(result['cpuPhaseMs']['gpuWait']['mean'], 6)
        self.assertEqual(result['cpuPhaseMs']['gpuWait']['count'], 11)
        self.assertEqual(result['cpuPhaseMsPerFreshOutput']['gpuWait'], 20)
        legacy = parse(fixture(), 3, 10)
        self.assertIsNone(legacy['rendererCpuMs'])
        self.assertEqual(legacy['cpuPhaseMs'], {})

    def test_cpu_phase_corruption_rejected(self):
        for text in [fixture(cpu=True).replace('"rendererCpuMs": 10', '"rendererCpuMs": 9'),
                     fixture(cpu=True).replace('"gpuWait": 6', '"gpuWait": -6')]:
            with self.assertRaises(ValueError):
                parse(text, 3, 10)

    def test_chained_has_no_admission_wait_metric(self):
        result = parse(fixture().replace('inputWaitMs=2 admittedWaitMs=1',
                                        'inputWaitMs=N/A admittedWaitMs=N/A'), 3, 10)
        self.assertIsNone(result['packetLatencyMs']['inputWaitMs'])
        self.assertIsNone(result['packetLatencyMs']['admittedWaitMs'])
        self.assertEqual(result['packetLatencyMs']['totalMs']['count'], 3)

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
