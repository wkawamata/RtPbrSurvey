"""Paired constant-G texture and equivalent scalar Volume outgoing directions."""
import argparse
from pathlib import Path

import numpy as np

from transmission_texture_fixture import write_json
from validate_part1 import sha
from volume_capture import capture_volume
from volume_roughness_texture_fixture import write_fixture


def assess(a, b):
    if a.shape != b.shape or a.ndim != 3 or a.shape[2] != 4:
        raise ValueError("Mismatched direction images")
    if not np.isfinite(a).all() or not np.isfinite(b).all():
        raise ValueError("Nonfinite direction images")
    active = np.linalg.norm(a[..., :3], axis=2) > .1
    if active.sum() < 128:
        raise ValueError("Insufficient nonzero outgoing directions")
    error = float(np.max(np.abs(a - b)))
    if error > .002:
        raise ValueError(f"Texture/scalar path mismatch: {error}")
    return dict(maxError=error, tolerance=.002, activePixels=int(active.sum()))


def run(exe, output):
    output.mkdir(parents=True, exist_ok=False)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    hashes = dict(executable=sha(exe), shader=sha(shader))
    report = dict(status="running", hashes=hashes, captures=[])
    write_json(output / "report.json", report)
    images = []
    try:
        for name, textured, factor in (("textured", True, .7), ("scalar", False, .7*128/255),
                                      ("smooth", False, 0)):
            folder = output / name
            write_fixture(folder, factor=factor, textured=textured)
            evidence, image = capture_volume(exe, folder, 1, resource="CurrentFrameRadiance", window_size=(256, 256))
            images.append(image)
            report["captures"].append(dict(name=name, **evidence))
            write_json(output / "report.json", report)
        report["assessment"] = assess(*images[:2])
        difference = float(np.max(np.abs(images[0] - images[2])))
        if difference <= .01:
            raise ValueError("Rough texture did not change paths relative to smooth control")
        report["smoothControlMaxDifference"] = difference
        if hashes != dict(executable=sha(exe), shader=sha(shader)):
            raise ValueError("Binaries changed during capture")
        report["status"] = "passed-constant-volume-roughness-texture"
    except Exception as error:
        report.update(status="failed", failure=str(error))
        raise
    finally:
        write_json(output / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.exe.resolve(strict=True), args.output.resolve())
