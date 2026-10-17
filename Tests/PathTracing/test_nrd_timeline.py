import copy
from pathlib import Path
import unittest

import numpy as np

from validate_nrd_timeline import CHECKPOINTS, KEYFRAMES, make_plan, check_scheduled, check_frame


class TimelineTests(unittest.TestCase):
    def test_reversal_and_settling_plan(self):
        plan = make_plan()
        self.assertEqual(len(plan['captures']), 8)
        frames, yaws = np.asarray(KEYFRAMES).T
        self.assertEqual(np.interp(36, frames, yaws), np.interp(44, frames, yaws))
        self.assertGreater(np.interp(36, frames, yaws)-np.interp(35, frames, yaws), 0)
        self.assertLess(np.interp(44, frames, yaws)-np.interp(43, frames, yaws), 0)
        for frame in (80, 84, 96, 112):
            self.assertEqual(np.interp(frame, frames, yaws), -20)

    def test_scheduled_inventory(self):
        folder = Path('timeline-test').resolve()
        records = [dict(case=case, frame=frame, path=str(folder/(case+'-pt.ptbuf')))
            for case, frame, _, _ in CHECKPOINTS]
        check_scheduled(records, folder)
        for mutation in ('duplicate', 'frame', 'path', 'missing'):
            other = copy.deepcopy(records)
            if mutation == 'duplicate':
                other[-1] = other[0]
            elif mutation == 'frame':
                other[-1]['frame'] += 1
            elif mutation == 'path':
                other[-1]['path'] = str(folder/'other.ptbuf')
            else:
                other.pop()
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                check_scheduled(other, folder)

    def test_two_clocks_and_no_reset(self):
        matrix = np.eye(4).tolist()
        flat = np.eye(4).ravel().tolist()
        meta = dict(viewProjection=matrix, previousViewProjection=matrix, sampleStartIndex=4,
            samplesPerFrame=1, primarySampling='pixel-center',
            nrdLastEvaluation=dict(continuousCameraHistory=True, dispatchCount=10,
                frameIndex=84, reset=False, worldToView=flat, viewToClip=flat,
                previousWorldToView=flat, previousViewToClip=flat, motionVectorScale=[.5, -.5, 0]))
        check_frame(meta, 84, False, 4)
        for state in ('reset', 'index', 'sample'):
            other = copy.deepcopy(meta)
            if state == 'reset':
                other['nrdLastEvaluation']['reset'] = True
            elif state == 'index':
                other['nrdLastEvaluation']['frameIndex'] = 0
            else:
                other['sampleStartIndex'] = 0
            with self.subTest(state=state), self.assertRaises(ValueError):
                check_frame(other, 84, False, 4)


if __name__ == '__main__':
    unittest.main()
