"""Contract tests for native guide parsing, motion units, and camera-axis depth."""
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np
from validate_inputs import read_buffer, project, srgb_texture_material, hash_uint, plane_hits


class InputTests(unittest.TestCase):
    def test_native_half_signed_rg_and_orientation(self):
        meta = dict(schemaVersion=1, rowOrder='top-down', format=34, width=2, height=2)
        values = np.array([[[.125, -.25], [.5, -.5]], [[1, -1], [2, -2]]], dtype='<f2')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'test.ptbuf'
            path.write_bytes(b'PTBUF1\n'+json.dumps(meta).encode()+b'\n'+values.tobytes())
            header, actual = read_buffer(path)
            np.testing.assert_array_equal(actual, values.astype(float))
            path.write_bytes(path.read_bytes()[:-1])
            with self.assertRaises(ValueError):
                read_buffer(path)

    def test_float_single_channel_and_reject_nonfinite(self):
        meta = dict(schemaVersion=1, rowOrder='top-down', format=41, width=1, height=1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'test.ptbuf'
            prefix = b'PTBUF1\n'+json.dumps(meta).encode()+b'\n'
            path.write_bytes(prefix+np.array([5], dtype='<f4').tobytes())
            self.assertEqual(read_buffer(path)[1][0, 0, 0], 5)
            path.write_bytes(prefix+np.array([float('nan')], dtype='<f4').tobytes())
            with self.assertRaises(ValueError):
                read_buffer(path)

    def test_previous_minus_current_ndc_and_pixel_units(self):
        current = np.eye(4)
        previous = np.eye(4)
        previous[0, 3] = .02
        previous[1, 3] = -.04
        points = np.array([[0, 0, 5], [1, 2, 5]])
        motion = project(points, previous)-project(points, current)
        np.testing.assert_allclose(motion[:, :2], [[.02, -.04], [.02, -.04]], atol=1e-15)
        np.testing.assert_allclose(motion[:, :2]*[1000/2, -500/2], [[10, 10], [10, 10]], atol=1e-12)

    def test_texture_color_space_and_shared_roughness_texture(self):
        decoded, roughness, bytes_ = srgb_texture_material([.25, .5, .75], .37)
        self.assertEqual(bytes_, [137, 188, 225])
        np.testing.assert_allclose(decoded, [.2501582847, .5028864580, .7529422168], atol=1e-9)
        self.assertAlmostEqual(roughness, .37*188/255)
        self.assertGreater(abs(roughness-.37), .09)

    def test_shader_hash_unsigned_wrap(self):
        self.assertEqual(int(hash_uint(0)), 0)
        # Independent scalar implementation, including overflow after every multiplication.
        def scalar(value):
            value ^= value >> 16
            value = value*0x7feb352d & 0xffffffff
            value ^= value >> 15
            value = value*0x846ca68b & 0xffffffff
            return value ^ (value >> 16)
        for value in [1, 0xffffffff, 84, 123456789]:
            self.assertEqual(int(hash_uint(value)), scalar(value))

    def test_off_axis_direction_is_not_view_axis_depth(self):
        # Independent geometric counterexample: plane z=5, camera forward +Z.
        ray = np.array([.25, -.1, 1])
        ray /= np.linalg.norm(ray)
        points = np.array([[-2, 0, 5], [0, 0, 5], [2, 0, 5]])
        axis = points @ np.array([0, 0, 1])
        off_axis = points @ ray
        np.testing.assert_array_equal(axis, [5, 5, 5])
        self.assertGreater(np.ptp(off_axis), .9)


if __name__ == '__main__':
    unittest.main()
