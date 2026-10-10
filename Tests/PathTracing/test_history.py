import copy
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from validate_history import CASES, assess_images, normalize_accumulation, validate_records
from validate_inputs import read_buffer


def records():
    result = [dict(case=name, accumulatedSamples=count, frameSampleIndex=index,
                 historyValid=valid, paused=True, randomSeed=11, samplesPerFrame=batch,
                 renderWidth=2, renderHeight=2)
            for name, (count, index, valid, batch) in CASES.items()]
    for record in result:
        if record["case"].startswith("resize-"):
            record.update(renderWidth=1280, renderHeight=720)
        if record["case"].endswith("-changed-16"):
            reason = dict(camera="Camera", light="Lighting", material="Material", geometry="Scene", resize="Render Size")
            record["resetReason"] = reason[record["case"].split("-")[0]]
            record["entryResetReason"] = "Pending Resize" if record["case"].startswith("resize-") else record["resetReason"]
        elif record["case"].endswith("-fresh-16"):
            record["resetReason"] = "Manual"
    return result


def images():
    result = {name: np.array([1.0, 2.0, 3.0], dtype=np.float32) for name in CASES}
    for name in ("resumed-32", "fresh-32", "batch-4-32"):
        result[name] = np.array([1.1, 2.1, 3.1], dtype=np.float32)
    result["reset-paused-0"] = np.zeros(3, dtype=np.float32)
    for index, change in enumerate(("camera", "light", "material", "geometry", "resize")):
        value = np.array([1.2 + index, 2.2 + index, 3.2 + index], dtype=np.float32)
        result[f"{change}-changed-16"] = value.copy()
        result[f"{change}-fresh-16"] = value.copy()
    return result


class HistoryValidationTests(unittest.TestCase):
    def test_gpu_sample_counts_and_normalization(self):
        raw = np.array([[[2, 4, 6, 2], [4, 6, 8, 2]]], dtype=np.float32)
        np.testing.assert_array_equal(normalize_accumulation(raw, 2), [1, 2, 3, 2, 3, 4])
        raw[0, 1, 3] = 3
        with self.assertRaises(ValueError):
            normalize_accumulation(raw, 2)

    def test_zero_count_requires_zero_radiance(self):
        raw = np.zeros((1, 2, 4), dtype=np.float32)
        self.assertEqual(np.count_nonzero(normalize_accumulation(raw, 0)), 0)
        raw[0, 1, 0] = 1
        with self.assertRaises(ValueError):
            normalize_accumulation(raw, 0)

    def test_raw_float32_accumulation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reset.ptbuf"
            meta = dict(schemaVersion=1, rowOrder="top-down", width=1, height=1, format=2,
                        resource="PathTracing.Accumulation")
            path.write_bytes(b"PTBUF1\n" + json.dumps(meta).encode() + b"\n" +
                             np.zeros(4, dtype="<f4").tobytes())
            stored, raw = read_buffer(path)
            self.assertEqual(stored["resource"], "PathTracing.Accumulation")
            self.assertEqual(raw.shape, (1, 1, 4))
            self.assertEqual(np.count_nonzero(raw), 0)

    def test_valid_records(self):
        self.assertEqual(validate_records(records(), list(CASES)), (2, 2))

    def test_reject_missing_or_duplicate(self):
        with self.assertRaises(ValueError):
            validate_records(records()[:-1], list(CASES))
        with self.assertRaises(ValueError):
            validate_records(records(), list(CASES) + ["fresh-32"])

    def test_reject_state_mismatches(self):
        for field, value in (("accumulatedSamples", 99), ("frameSampleIndex", 99),
                             ("historyValid", False), ("paused", False),
                             ("randomSeed", 12), ("samplesPerFrame", 2)):
            changed = copy.deepcopy(records())
            changed[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_records(changed, list(CASES))

    def test_reject_dimensions(self):
        changed = records()
        changed[1]["renderWidth"] = 3
        with self.assertRaises(ValueError):
            validate_records(changed, list(CASES))

    def test_reject_reset_reason(self):
        changed = records()
        changed[9]["resetReason"] = "Manual"
        with self.assertRaises(ValueError):
            validate_records(changed, list(CASES))

    def test_reject_resize_not_applied(self):
        changed = records()
        changed[-1]["renderWidth"] = 1920
        with self.assertRaises(ValueError):
            validate_records(changed, list(CASES))

    def test_reject_changed_state_contamination(self):
        pixels = images()
        pixels["material-changed-16"][0] += 0.1
        with self.assertRaises(ValueError):
            assess_images(pixels)

    def test_reject_no_mutation_signal(self):
        pixels = images()
        pixels["camera-changed-16"] = pixels["baseline-16"].copy()
        pixels["camera-fresh-16"] = pixels["baseline-16"].copy()
        with self.assertRaises(ValueError):
            assess_images(pixels)

    def test_exact_replay_and_roundoff(self):
        pixels = images()
        pixels["batch-4-32"][0] += 1e-7
        self.assertEqual(assess_images(pixels)["resetMaxAbs"], 0)

    def test_reject_contamination(self):
        for name in ("paused-16", "reset-repeat-16", "resumed-32",
                     "non-accumulated-repeat-12", "reset-paused-0", "batch-4-32"):
            pixels = images()
            pixels[name][0] += 0.1
            with self.subTest(case=name), self.assertRaises(ValueError):
                assess_images(pixels)

    def test_reject_absent_stochastic_signal(self):
        pixels = images()
        for name in ("resumed-32", "fresh-32", "batch-4-32"):
            pixels[name] = pixels["baseline-16"].copy()
        with self.assertRaises(ValueError):
            assess_images(pixels)

    def test_reject_nan(self):
        pixels = images()
        pixels["baseline-16"][0] = np.nan
        with self.assertRaises(ValueError):
            assess_images(pixels)


if __name__ == "__main__":
    unittest.main()
