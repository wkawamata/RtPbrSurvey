import unittest

from validate_nrd_prepass_sensitivity import PROFILES, FRAMES, EXPECTED, check_profile, summarize


class PrepassSensitivityTests(unittest.TestCase):
    def test_profile_requires_applied_independent_radii(self):
        for profile, (diffuse, specular) in PROFILES.items():
            meta = dict(nrdPrepass=dict(profile=profile, applied=True,
                diffuseRadius=diffuse, specularRadius=specular))
            check_profile(meta, profile)
            for key, value in (('applied', False), ('diffuseRadius', diffuse+1),
                               ('specularRadius', specular+1), ('profile', 'wrong')):
                with self.assertRaises(ValueError):
                    check_profile(dict(nrdPrepass=dict(meta['nrdPrepass'], **{key: value})), profile)

    def rows(self):
        return [dict(profile=profile, frames=frames, roi=roi, meanRgbBias=.1 if profile == 'control' else .05,
                     passed=profile != 'control') for profile in PROFILES for frames in FRAMES for roi in EXPECTED]

    def test_summary_reports_all_rois_and_recoveries(self):
        rows = summarize(self.rows())
        self.assertEqual(len(rows), 56)
        self.assertTrue(all(row['recovered'] and not row['regression'] for row in rows))
        self.assertTrue(all(abs(row['meanRgbBiasDelta']+.05) < 1e-10 for row in rows))

    def test_summary_rejects_missing_duplicate_or_unknown_identity(self):
        rows = self.rows()
        for changed in (rows[:-1], rows[:-1]+[rows[0]], rows+[rows[0]],
                        rows[:-1]+[dict(rows[-1], profile='unknown')]):
            with self.assertRaises(ValueError):
                summarize(changed)


if __name__ == '__main__':
    unittest.main()
