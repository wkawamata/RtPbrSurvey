"""Patterned G-channel invariance and UV-dependence probes for solid Volume."""
import argparse
from pathlib import Path
import numpy as np
from transmission_texture_fixture import write_json
from validate_part1 import sha
from validate_volume_roughness_texture import assess
from volume_roughness_texture_fixture import write_fixture
from volume_capture import capture_volume


def run(exe, output):
    output.mkdir(parents=True, exist_ok=False)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    hashes = dict(executable=sha(exe), shader=sha(shader))
    report = dict(status="running", hashes=hashes, captures=[])
    images = []
    write_json(output / "report.json", report)
    try:
        for name, options in (("pattern", {}), ("other-channels", dict(alternate_channels=True)),
                              ("flipped-uv", dict(flip_uv=True))):
            folder = output / name
            write_fixture(folder, patterned=True, **options)
            evidence, image = capture_volume(exe, folder, 1, resource="CurrentFrameRadiance", window_size=(256, 256))
            images.append(image)
            report["captures"].append(dict(name=name, **evidence))
            write_json(output / "report.json", report)
        report["channelInvariance"] = assess(images[0], images[1])
        if not np.isfinite(images[2]).all():
            raise ValueError("Nonfinite flipped-UV evidence")
        changed = np.max(np.abs(images[0] - images[2]), axis=2) > .01
        if changed.sum() < 128:
            raise ValueError("Pattern failed to produce UV-dependent paths")
        report["uvChangedPixels"] = int(changed.sum())
        if hashes != dict(executable=sha(exe), shader=sha(shader)):
            raise ValueError("Binaries changed")
        report["status"] = "passed-patterned-volume-roughness-probes"
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
