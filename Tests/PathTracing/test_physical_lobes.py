import unittest
import numpy as np
from validate_physical_lobes import diffuse_fraction, assess, RESOURCES


class PhysicalLobeTests(unittest.TestCase):
    def evaluate(self, metallic):
        return diffuse_fraction(np.array([[.3, .5, .7]]), metallic, np.array([.55]),
            np.array([[0., 1., 0.]]), np.array([[0., 1., 0.]]), np.array([[0., 1., 0.]]))

    def test_mixed_components_are_not_binary_proposal_labels(self):
        fraction = self.evaluate(.35)
        self.assertTrue((fraction > 0).all() and (fraction < 1).all())

    def test_metal_has_no_diffuse_component(self):
        np.testing.assert_array_equal(self.evaluate(1), 0)

    def test_component_partition_reconstructs_shared_transport(self):
        fraction = self.evaluate(.35)
        transport = np.array([[.25, 2, 4]])
        np.testing.assert_allclose(fraction*transport+(1-fraction)*transport, transport)

    def fixture(self):
        matrix = np.eye(4).tolist()
        buffers = {}
        fraction = self.evaluate(.35)[0]
        for name in RESOURCES:
            values = np.zeros((128, 128, 4))
            meta = dict(resource='PathTracing.'+name, format=10 if name in ('Albedo', 'NormalRoughness') else 2,
                width=128, height=128, randomSeed=7, sampleStartIndex=30, viewProjection=matrix,
                previousViewProjection=matrix, inverseViewProjection=matrix)
            if name == 'PhysicalDiffuse':
                values[..., :3] = fraction
            elif name == 'PhysicalSpecular':
                values[..., :3] = 1-fraction
            elif name == 'CurrentFrameRadiance':
                values[:] = 1
            elif name == 'PrimaryContinuation':
                values[..., 2] = -1
                values[..., 3] = .5
            elif name == 'NormalRoughness':
                values[..., 2] = -1
                values[..., 3] = .55
            elif name == 'Albedo':
                values[..., :3] = [.3, .5, .7]
                values[..., 3] = 1
            elif name == 'SecondaryRayData':
                values[..., 0] = 100
                values[..., 1] = 1
                values[:, 16::16, 1] = 2
                values[..., 2] = 2
            buffers[name] = (meta, values)
        return buffers

    def test_independent_oracle_and_reconstruction(self):
        self.assertTrue(assess(self.fixture())['passed'])

    def test_readback_normals_are_renormalized(self):
        buffers = self.fixture()
        buffers['NormalRoughness'][1][..., :3] *= .9995
        self.assertTrue(assess(buffers)['passed'])

    def test_invalid_primary_normal_rejected(self):
        buffers = self.fixture()
        buffers['NormalRoughness'][1][..., :3] = 0
        with self.assertRaises(ValueError):
            assess(buffers)

    def test_proposal_only_split_fails_oracle(self):
        buffers = self.fixture()
        buffers['PhysicalDiffuse'][1][..., :3] = 1
        buffers['PhysicalSpecular'][1][..., :3] = 0
        self.assertFalse(assess(buffers)['checks']['mixedFraction'])

    def test_broken_reconstruction_rejected(self):
        buffers = self.fixture()
        buffers['PhysicalDiffuse'][1][..., :3] *= 2
        self.assertFalse(assess(buffers)['checks']['reconstruction'])

    def test_missing_and_mismatched_cohorts_rejected(self):
        buffers = self.fixture()
        del buffers['PhysicalSpecular']
        with self.assertRaises(ValueError):
            assess(buffers)
        buffers = self.fixture()
        buffers['PhysicalSpecular'][0]['sampleStartIndex'] += 1
        with self.assertRaises(ValueError):
            assess(buffers)

    def test_mixed_primary_modes_and_sample_batches_rejected(self):
        buffers = self.fixture()
        buffers['PhysicalSpecular'][0]['primarySampling'] = 'pixel-center'
        with self.assertRaises(ValueError):
            assess(buffers)
        buffers = self.fixture()
        buffers['CurrentFrameRadiance'][0]['samplesPerFrame'] = 4
        with self.assertRaises(ValueError):
            assess(buffers)


if __name__ == '__main__':
    unittest.main()
