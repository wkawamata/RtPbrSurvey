"""Runtime smoke coverage of rough Cube/Sphere; not an energy/reference-image test."""
import argparse
from pathlib import Path

import numpy as np

from volume_boundary_fixture import write_volume_fixture
from volume_capture import capture_volume
from transmission_texture_fixture import write_json
from validate_part1 import sha


def run(exe, output):
    output.mkdir(parents=True, exist_ok=False)
    executable_hash = sha(exe)
    records = []
    for sphere in (False, True):
        images = []
        for roughness in (0, .35):
            name = ("sphere" if sphere else "cube") + ("-rough" if roughness else "-smooth")
            folder = output / name
            scene, _ = write_volume_fixture(folder, mode=3, ior=1.5, sphere=sphere, bounces=16)
            scene["materials"][0]["roughness"] = roughness
            write_json(folder / "scene.json", scene)
            evidence, image = capture_volume(exe, folder, 32)
            if not np.isfinite(image).all() or np.min(image[..., :3]) < 0:
                raise ValueError("Non-finite or negative radiance: " + name)
            h, w = image.shape[:2]
            roi = image[h//2-24:h//2+24, w//2-24:w//2+24, :3]
            if np.mean(roi) <= 0:
                raise ValueError("Black center: " + name)
            images.append(roi)
            records.append(dict(name=name, roughness=roughness, mean=float(np.mean(roi)), evidence=evidence))
        difference = float(np.mean(np.abs(images[0] - images[1])))
        if difference <= 1e-4:
            raise ValueError("Roughness has no measurable effect")
        records[-1]["smoothRoughDifference"] = difference
    if sha(exe) != executable_hash:
        raise ValueError("Executable changed during capture")
    write_json(output / "report.json", dict(passed=True, executableSha256=executable_hash,
        scope="runtime smoke, not energy qualification", cases=records))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.exe.resolve(), args.output.resolve())
