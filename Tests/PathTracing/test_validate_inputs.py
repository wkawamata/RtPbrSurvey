"""Contract tests for native guide parsing, motion units, and camera-axis depth."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
from validate_inputs import read_buffer, project, srgb_texture_material, hash_uint, plane_hits, projection_plane_forward
import validate_inputs


class InputTests(unittest.TestCase):
    def test_native_unorm_rgba(self):
        meta = dict(schemaVersion=1, rowOrder='top-down', format=28, width=1, height=1)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'test.ptbuf'
            path.write_bytes(b'PTBUF1\n'+json.dumps(meta).encode()+b'\n'+bytes([0, 128, 255, 64]))
            np.testing.assert_allclose(read_buffer(path)[1][0, 0], [0, 128/255, 1, 64/255])

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

    def test_texture_color_space_and_independent_roughness(self):
        decoded, roughness, bytes_ = srgb_texture_material([.25, .5, .75], .37)
        self.assertEqual(bytes_, [137, 188, 225])
        np.testing.assert_allclose(decoded, [.2501582847, .5028864580, .7529422168], atol=1e-9)
        self.assertAlmostEqual(roughness, .37)

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

    def test_projection_depth_axis_with_shift_roll_and_translation(self):
        eye = np.array([3, 2, -5.])
        forward = np.array([-4, -1.5, 6.])
        forward /= np.linalg.norm(forward)
        right = np.cross([.2, 1, .1], forward)
        right /= np.linalg.norm(right)
        up = np.cross(forward, right)
        view = np.eye(4)
        view[:3, :3] = np.array([right, up, forward]).T
        view[3, :3] = -eye @ view[:3, :3]
        for orthographic in [False, True]:
            for shift in [0, .35, -.8]:
                near, far = .1, 1000
                if orthographic:
                    projection = np.diag([.2, 1/3, 1/(far-near), 1.])
                    projection[3, 2] = -near/(far-near)
                else:
                    projection = np.zeros((4, 4))
                    projection[0, 0], projection[1, 1] = 1.1, 1.9
                    projection[2, :3] = [-shift, .2, far/(far-near)]
                    projection[2, 3] = 1
                    projection[3, 2] = -near*far/(far-near)
                inverse = np.linalg.inv(view @ projection).T
                np.testing.assert_allclose(projection_plane_forward(inverse), forward, atol=1e-12)

    def test_off_axis_direction_is_not_view_axis_depth(self):
        # Independent geometric counterexample: plane z=5, camera forward +Z.
        ray = np.array([.25, -.1, 1])
        ray /= np.linalg.norm(ray)
        points = np.array([[-2, 0, 5], [0, 0, 5], [2, 0, 5]])
        axis = points @ np.array([0, 0, 1])
        off_axis = points @ ray
        np.testing.assert_array_equal(axis, [5, 5, 5])
        self.assertGreater(np.ptp(off_axis), .9)

    def run_numeric_report(self, result):
        with tempfile.TemporaryDirectory() as directory:
            arguments = ['validate_inputs.py', '--analyze-only', '--output', str(Path(directory)/'captures'),
                '--cases', 'input-plane-'+result['resource']+'-static']
            with patch('sys.argv', arguments), \
                patch.object(validate_inputs, 'ROOT', Path(directory)), \
                patch.object(validate_inputs.subprocess, 'check_output', side_effect=['commit', 'branch', '', 'GPU']), \
                patch.object(validate_inputs, 'sha', return_value='hash'), \
                patch.object(Path, 'read_text', return_value=''), \
                patch.object(validate_inputs, 'analyze', return_value=({}, result)), \
                patch.object(validate_inputs, 'write_json') as write, \
                patch('builtins.print'):
                exit_failure = validate_inputs.main()
                report = write.call_args.args[1]
                return exit_failure, report

    def test_numeric_failure_retains_result_and_fails_run(self):
        for resource in ['NormalRoughness', 'Albedo', 'ViewZ']:
            with self.subTest(resource=resource):
                result = dict(resource=resource, passed=False)
                failure, report = self.run_numeric_report(result)
                self.assertTrue(failure)
                self.assertEqual(report['status'], 'incomplete')
                self.assertEqual(len(report['failures']), 1)
                self.assertEqual(report['records'][0]['result'], result)

    def test_passing_numeric_capture_completes_run(self):
        failure, report = self.run_numeric_report(dict(resource='ViewZ', passed=True))
        self.assertFalse(failure)
        self.assertEqual(report['status'], 'done')
        self.assertEqual(report['failures'], [])

    def test_motion_uses_format_bound_and_retains_absolute_diagnostic(self):
        for format_passed in [True, False]:
            result = dict(resource='MotionVectors', passed=False, halfPrecisionBoundPassed=format_passed)
            failure, report = self.run_numeric_report(result)
            self.assertEqual(failure, not format_passed)
            self.assertFalse(report['records'][0]['result']['passed'])

    def test_material_mismatch_fails_otherwise_passing_capture(self):
        failure, report = self.run_numeric_report(dict(resource='Albedo', passed=True,
            primaryHitMaterialMismatchCount=1))
        self.assertTrue(failure)
        self.assertEqual(report['status'], 'incomplete')


if __name__ == '__main__':
    unittest.main()
