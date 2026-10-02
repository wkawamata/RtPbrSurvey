import unittest
from validate_scale import make_case, normalized_profile, compare_visibility, contact_roi_contract, segment_hits_cube


class ScaleTests(unittest.TestCase):
    def test_contact_roi_is_visible_floor(self):
        self.assertTrue(contact_roi_contract()["primaryRaysMissCube"])
        self.assertTrue(segment_hits_cube([0, 2, 0], [0, 0, 0]))
        self.assertFalse(segment_hits_cube([2, 2, 0], [2, 0, 0]))
    def test_lighting_and_geometry_similarity(self):
        scene,preset=make_case(.01)
        light=preset["lighting"]["lights"][0]
        self.assertEqual(light["position"],[-.02,.03,0])
        self.assertAlmostEqual(light["intensity"],.0008)
        self.assertEqual(scene["camera"]["position"],[0,.04,-.07])
        self.assertEqual(scene["nodes"][0]["scale"],[.01,.01,.01])
        self.assertAlmostEqual(preset["shadow"]["normalBias"],.0001)

    def test_bias_and_tmin_are_separated(self):
        _,bias=make_case(.01,policy="fixed-bias")
        _,tmin=make_case(.01,policy="fixed-tmin")
        self.assertEqual(bias["shadow"]["normalBias"],.01)
        self.assertAlmostEqual(bias["shadow"]["rayTMin"],.00001)
        self.assertAlmostEqual(tmin["shadow"]["normalBias"],.0001)
        self.assertEqual(tmin["shadow"]["rayTMin"],.001)

    def test_beyond_slab_is_after_light_independent_of_bias(self):
        for policy in ("relative","fixed"):
            scene,preset=make_case(.01,variant="beyond",policy=policy)
            slab=scene["nodes"][1]
            self.assertGreater(slab["translation"][1]-.5*slab["scale"][1],preset["lighting"]["lights"][0]["position"][1])

    def test_profile_and_visibility(self):
        self.assertEqual(normalized_profile([0,0,0,2,2,2],[1,1,1,2,2,2],2),[0,1])
        self.assertTrue(compare_visibility([1,1,1],[0,0,0],[1,1,1])["passed"])
        self.assertFalse(compare_visibility([1,1,1],[1,1,1],[1,1,1])["passed"])


if __name__=="__main__":
    unittest.main()
