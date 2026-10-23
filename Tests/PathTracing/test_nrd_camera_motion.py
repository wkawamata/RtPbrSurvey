import copy
import unittest

import numpy as np

from validate_nrd_camera_motion import assess_frame, assess_motion


class NrdCameraMotionTests(unittest.TestCase):
    def setUp(self):
        matrix = np.eye(4).tolist()
        flat = np.eye(4).ravel().tolist()
        self.meta = dict(viewProjection=matrix, previousViewProjection=matrix,
            nrdLastEvaluation=dict(continuousCameraHistory=True, dispatchCount=12,
                frameIndex=30, reset=False, worldToView=flat, viewToClip=flat,
                previousWorldToView=flat, previousViewToClip=flat, motionVectorScale=[0.5, -0.5, 0.0]))

    def test_continued(self):
        self.assertEqual(assess_frame(self.meta, True, True)["frameIndex"], 30)

    def test_forced_reset_is_rejected(self):
        self.meta["nrdLastEvaluation"].update(frameIndex=0, reset=True)
        with self.assertRaises(ValueError):
            assess_frame(self.meta, True, True)

    def test_explicit_reset_control(self):
        self.meta["nrdLastEvaluation"].update(continuousCameraHistory=False, frameIndex=0, reset=True)
        self.assertTrue(assess_frame(self.meta, False, True)["reset"])

    def test_wrong_sign_or_scale(self):
        for scale in ([0.5, 0.5, 0], [-0.5, -0.5, 0], [1, -1, 0]):
            self.meta["nrdLastEvaluation"]["motionVectorScale"] = scale
            with self.assertRaises(ValueError):
                assess_frame(self.meta, True, True)

    def test_matrix_basis_and_nonfinite(self):
        for value in (0.1, float("nan")):
            meta = copy.deepcopy(self.meta)
            meta["nrdLastEvaluation"]["previousWorldToView"][12] = value
            with self.assertRaises(ValueError):
                assess_frame(meta, True, True)

    def test_no_dispatch_or_short_history(self):
        for values in (dict(dispatchCount=0), dict(frameIndex=2)):
            meta = copy.deepcopy(self.meta)
            meta["nrdLastEvaluation"].update(values)
            with self.assertRaises(ValueError):
                assess_frame(meta, True, True)

    def test_static_motion_uses_existing_numeric_bound(self):
        meta = dict(resource="PathTracing.MotionVectors", format=34, width=128, height=128,
            primarySampling="pixel-center", inverseViewProjection=np.eye(4).tolist(),
            viewProjection=np.eye(4).tolist(), previousViewProjection=np.eye(4).tolist(),
            rayTMin=0.001, rayTMax=10, nrdLastEvaluation=dict(motionVectorScale=[0.5, -0.5, 0]))
        scene = dict(nodes=[dict(type="primitive", primitive=dict(kind="plane", width=2, depth=2),
            scale=[1, 1, 1], translation=[0, 0, 0.5], rotation=[-2**-0.5, 0, 0, 2**-0.5])])
        tiny = np.full((128, 128, 2), 1e-7)
        self.assertLess(assess_motion(meta, tiny, scene, False)["maxBoundNormalizedError"], 1)
        with self.assertRaises(ValueError):
            assess_motion(meta, tiny * 40, scene, False)


if __name__ == "__main__":
    unittest.main()
