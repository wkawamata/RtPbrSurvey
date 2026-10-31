import copy
from pathlib import Path
import unittest
from unittest.mock import patch

import numpy as np

from compare_nrd_refined_components import validate_report, load_refined, SAMPLES, CHANNELS, SEEDS, EXPECTED
from refine_nrd_component_references import summarize
from qualify_nrd_component_references import qualify
from compare_nrd_component_references import assess, joint_assessment


class RefinedComparisonTests(unittest.TestCase):
    def report(self):
        mask = np.ones((10, 10), dtype=bool)
        rows = []
        for component in CHANNELS:
            for roi in EXPECTED:
                image = np.ones((10, 10, 4), dtype=np.float32)
                if component == 'diffuse' and roi.startswith('metal-'):
                    image[..., :3] = 0
                rows.append(qualify(image, image, mask, component, roi))
        return dict(schemaVersion=1, status='completed-component-reference-refinement', samples=SAMPLES,
            seeds=list(SEEDS), samplesPerFrame=1, pairNrmseLimit=.03, windowMode='hidden', freshGpuCaptures=4,
            reusedTotalCapturesRevalidated=2, fullIntegrationReady=False, masksReselected=False, activeCapture=None,
            failures=[], records=[dict(component=c, seed=s, samples=SAMPLES,
                acquisition='revalidated-total' if c == 'total' else 'fresh') for c in CHANNELS for s in SEEDS],
            rois=rows, summary=summarize(rows), referenceVerdict='qualified-all-components')

    def test_completed_partial_reference_is_valid_but_not_all_qualified(self):
        report = self.report()
        validate_report(report)
        row = next(r for r in report['rois'] if r['component'] == 'diffuse' and r['roi'].endswith('mirror-proxy')
            and r['roi'].startswith('dielectric-'))
        row.update(status='inconclusive-reference', qualified=False, referencePairNrmse=.05)
        report.update(summary=summarize(report['rois']), referenceVerdict='partially-inconclusive')
        validate_report(report)
        with self.assertRaises(ValueError):
            validate_report(dict(report, referenceVerdict='qualified-all-components'))

    def test_missing_duplicate_or_wrong_role_captures_are_rejected(self):
        report = self.report()
        for records in (report['records'][:-1], report['records'][:-1]+[report['records'][0]],
                [dict(r, acquisition='fresh') for r in report['records']]):
            with self.assertRaises(ValueError):
                validate_report(dict(report, records=records))

    def test_policy_state_or_numeric_evidence_cannot_be_omitted(self):
        report = self.report()
        for key in ('activeCapture', 'samples', 'windowMode', 'summary'):
            changed = copy.deepcopy(report)
            del changed[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_report(changed)
        changed = copy.deepcopy(report)
        del changed['rois'][0]['referencePairNrmse']
        with self.assertRaises(ValueError):
            validate_report(changed)
        with self.assertRaises(ValueError):
            validate_report(dict(report, samples=8192))

    def test_unreviewed_report_is_rejected_before_loading_captures(self):
        with patch('compare_nrd_refined_components.sha', return_value='0'*64):
            with self.assertRaisesRegex(ValueError, 'frozen reviewed'):
                load_refined(Path('refined'), Path('pairs'), Path('source'))

    def test_focus_total_and_specular_remain_assessable_without_diffuse(self):
        roi = 'dielectric-smooth/mirror-proxy'
        mask = np.ones((10, 10), dtype=bool)
        first = np.ones((10, 10, 4), dtype=np.float32)
        second = first.copy()
        second[..., :3] = 1.1
        rows = []
        for component in CHANNELS:
            other = second if component == 'diffuse' else first
            rows.append(assess(first[..., :3], first, other, mask, component, roi,
                qualify(first, other, mask, component, roi)))
        self.assertEqual([r['passed'] for r in rows], [True, None, True])
        self.assertEqual(joint_assessment(rows)['qualityVerdict'], 'not-assessed')


if __name__ == '__main__':
    unittest.main()
