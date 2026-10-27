import unittest

from audit_nrd_lobe_distance import audit
import test_secondary_geometry


class NrdLobeDistanceTests(unittest.TestCase):
    def setUp(self):
        fixture = test_secondary_geometry.SecondaryGeometryTests()
        self.buffers = fixture.fixture()
        self.scene = fixture.scene()

    def test_specular_ndf_is_not_accepted_as_vndf(self):
        report = audit(self.buffers, self.scene)
        self.assertEqual(report['pixelCounts']['diffuseProposalHits'], 8192)
        self.assertEqual(report['pixelCounts']['specularNdfProposalMisses'], 8192)
        self.assertFalse(report['nrdDistanceReady'])

    def test_diffuse_only_does_not_make_full_contract_ready(self):
        self.buffers['SecondaryRayData'][1][..., 1] = 1
        report = audit(self.buffers, self.scene)
        self.assertEqual(report['pixelCounts']['specularNdfProposalMisses'], 0)
        self.assertFalse(report['nrdDistanceReady'])

    def test_unknown_proposal_outside_oracle_roi_is_rejected(self):
        self.buffers['SecondaryRayData'][1][0, 0, 1] = 3
        with self.assertRaises(ValueError):
            audit(self.buffers, self.scene)

    def test_untraced_is_not_a_zero_distance_hit(self):
        self.buffers['SecondaryRayData'][1][0, 0] = 0
        self.buffers['SecondarySurfaceDistance'][1][0, 0] = 0
        report = audit(self.buffers, self.scene)
        self.assertEqual(report['pixelCounts']['untraced'], 1)
        self.assertFalse(report['nrdDistanceReady'])

    def test_nonfinite_distance_outside_oracle_roi_is_rejected(self):
        self.buffers['SecondarySurfaceDistance'][1][0, 0, 0] = float('nan')
        with self.assertRaises(ValueError):
            audit(self.buffers, self.scene)

    def test_miss_cannot_publish_finite_surface_distance(self):
        self.buffers['SecondarySurfaceDistance'][1][0, 100, 0] = 100
        with self.assertRaises(ValueError):
            audit(self.buffers, self.scene)


if __name__ == '__main__':
    unittest.main()
