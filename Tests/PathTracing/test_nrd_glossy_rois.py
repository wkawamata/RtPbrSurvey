import copy
import unittest
from unittest.mock import patch

import numpy as np

from validate_nrd_disocclusion import document_albedo
from validate_nrd_glossy_rois import (EXPECTED, LIMITS, MATERIALS, ROI_LIMITS, SELECTION,
    assess_roi, dilate, mirror_proxies, roi_verdict, select_rois, sphere_distance, validate_inventory)


class GlossyRoiTests(unittest.TestCase):
    def signals(self):
        reference = np.repeat(np.linspace(.1, 1, 200)[:, None], 3, axis=1)
        native = reference.copy()
        native[::2] *= 1.1
        native[1::2] *= .9
        return reference, native, reference, reference

    def test_roi_qualified_failure_and_inconclusive_are_distinct(self):
        values = self.signals()
        self.assertEqual(assess_roi(*values)['status'], 'passed-roi')
        self.assertEqual(assess_roi(values[0]*1.2, *values[1:])['status'], 'failed-roi-quality')
        self.assertEqual(assess_roi(*values[:3], values[3]*1.2)['status'], 'inconclusive-reference')
        self.assertEqual(assess_roi(*(v[:99] for v in values))['status'], 'inconclusive-coverage')
        self.assertEqual(assess_roi(*(v*0 for v in values))['status'], 'inconclusive-dark-reference')
        with self.assertRaises(ValueError):
            assess_roi(np.full((200, 3), np.nan), *values[1:])

    def test_verdict_requires_inventory_and_actual_consistent_measurements(self):
        rows = [dict(assess_roi(*self.signals()), roi=name) for name in sorted(EXPECTED)]
        self.assertEqual(roi_verdict(rows), 'passed-static-rois')
        for mutation in ('missing', 'duplicate', 'number', 'flag', 'checks', 'nan', 'coverage'):
            changed = copy.deepcopy(rows)
            if mutation == 'missing':
                changed.pop()
            elif mutation == 'duplicate':
                changed[-1] = changed[0]
            elif mutation == 'number':
                del changed[0]['meanRgbBias']
            elif mutation == 'flag':
                changed[0]['passed'] = False
            elif mutation == 'checks':
                changed[0]['checks'] = {}
            elif mutation == 'nan':
                changed[0]['referencePairNrmse'] = float('nan')
            else:
                changed[0]['pixels'] = 99
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                roi_verdict(changed)
        failed = assess_roi(self.signals()[0]*1.2, *self.signals()[1:])
        rows[0] = dict(failed, roi=rows[0]['roi'])
        self.assertEqual(roi_verdict(rows), 'failed-roi-quality')

    def test_reference_only_selection_is_repeatable_and_complete(self):
        albedo, reference = np.zeros((45, 45, 4)), np.zeros((45, 45, 4))
        for i, (_, color, _, _) in enumerate(MATERIALS):
            y, x = 1+22*(i//2), 1+22*(i % 2)
            albedo[y:y+20, x:x+20] = document_albedo(color)
            reference[y:y+20, x:x+20, :3] = np.linspace(.1, 1, 20)[None, :, None]
        proxies = {name: np.zeros((45, 45), dtype=bool) for name in EXPECTED if name.endswith('/mirror-proxy')}
        with patch('validate_nrd_glossy_rois.mirror_proxies', return_value=proxies):
            first, _ = select_rois({}, {}, albedo, reference)
            second, _ = select_rois({}, {}, albedo, reference)
        self.assertEqual(set(first), EXPECTED)
        self.assertTrue(all(np.array_equal(first[name], second[name]) for name in first))
        for name, _, _, _ in MATERIALS:
            self.assertFalse((first[name+'/highlight'] & first[name+'/dark']).any())
            self.assertFalse((first[name+'/highlight'] & first[name+'/border']).any())
        self.assertEqual(len(EXPECTED), 14)
        self.assertEqual(SELECTION['targetRadiusFactor'], .8)
        self.assertEqual(ROI_LIMITS['referencePairNrmse'], .03)

    def test_sphere_intersection_and_guarded_mirror_proxy(self):
        origins = np.array([[0., 0, -3], [3, 0, -3], [0, 0, 0]])
        directions = np.array([[0., 0, 1], [0, 0, 1], [0, 0, 1]])
        distance = sphere_distance(origins, directions, np.zeros(3), 1, .001, 100)
        self.assertEqual(distance[0], 2)
        self.assertTrue(np.isinf(distance[1]))
        self.assertEqual(distance[2], 1)
        positions = ([0, 0, -5], [0, 0, 0], [-10, 0, 0], [10, 0, 0])
        scene = dict(assets=[], nodes=[dict(materialId=m[0], translation=p, scale=[1, 1, 1],
            parentId=None, type='primitive', visible=True, primitive=dict(kind='sphere', radius=1))
            for m, p in zip(MATERIALS, positions)])
        masks = {m[0]: np.ones((2, 2), dtype=bool) for m in MATERIALS}
        rays = (origins[:1], directions[:1])
        with patch('validate_nrd_glossy_rois.primary_rays', return_value=rays):
            result = mirror_proxies(dict(rayTMin=.001, rayTMax=100, normalBias=.001), scene, masks)
        self.assertTrue(result['dielectric-smooth/mirror-proxy'][0, 0])
        self.assertFalse(result['metal-smooth/mirror-proxy'].any())

    def test_requalification_inventory_rejects_missing_duplicate_or_wrong_depth(self):
        report = dict(schemaVersion=1, records=[dict(label='reference-b'+str(d)+'-'+str(s))
            for d in (4, 8) for s in (11, 29)], failures=[], sourceEvidenceReexecuted=False,
            sourceRecordsRevalidated=12, limits=LIMITS,
            referenceSamplesByDepth={'2': 4096, '4': 8192, '8': 8192}, referenceSeeds=[11, 29])
        validate_inventory(report)
        for mutation in ('missing', 'duplicate', 'depth', 'rerun'):
            changed = copy.deepcopy(report)
            if mutation == 'missing':
                changed['records'].pop()
            elif mutation == 'duplicate':
                changed['records'][-1] = changed['records'][0]
            elif mutation == 'depth':
                changed['referenceSamplesByDepth']['4'] = 4096
            else:
                changed['sourceEvidenceReexecuted'] = True
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_inventory(changed)

    def test_border_dilation_does_not_wrap_image_edges(self):
        mask = np.zeros((5, 5), dtype=bool)
        mask[0, 0] = True
        expanded = dilate(mask, 1)
        self.assertEqual(int(expanded.sum()), 3)
        self.assertFalse(expanded[-1].any())
        self.assertFalse(expanded[:, -1].any())


if __name__ == '__main__':
    unittest.main()
