"""Paired per-path distance/absorption contract for rough primitive volumes."""
import argparse
from pathlib import Path

import numpy as np

from transmission_texture_fixture import write_json
from validate_part1 import sha
from validate_volume_absorption import expected_transmittance
from volume_absorption_fixture import COLOR, write_absorption_fixture
from volume_capture import capture_volume


def assess(distance, transmittance, status):
    if distance.shape != transmittance.shape or distance.shape != status.shape:
        raise ValueError("Mismatched paired image dimensions")
    if distance.ndim != 3 or distance.shape[2] != 4 or min(distance.shape[:2]) < 64:
        raise ValueError("Invalid image dimensions")
    if not all(np.isfinite(image).all() for image in (distance, transmittance, status)):
        raise ValueError("Non-finite paired evidence")
    h, w = distance.shape[:2]
    roi = np.s_[h//2-32:h//2+32, w//2-32:w//2+32]
    lengths = distance[roi][..., 2]
    codes = status[roi][..., 0]
    if np.any(lengths < 0) or not np.isin(codes, [0, 3, 7]).all():
        raise ValueError("Invalid distance or medium/boundary error")
    completed = (codes == 0) & (distance[roi][..., 0] == 1) & (distance[roi][..., 1] == 1)
    if np.count_nonzero(completed) < 512:
        raise ValueError("Insufficient completed transmission paths")
    expected = expected_transmittance(lengths, COLOR, 1)
    error = float(np.max(np.abs(transmittance[roi][..., :3] - expected)))
    if error > .002:
        raise ValueError(f"Per-path Beer-Lambert mismatch: {error}")
    return dict(maxError=error, tolerance=.002, completedPaths=int(completed.sum()),
        rejectedPaths=int((codes == 7).sum()), budgetTerminatedPaths=int((codes == 3).sum()),
        meanCompletedLength=float(lengths[completed].mean()), roi=[w//2-32, h//2-32, w//2+32, h//2+32])


def run(exe, output):
    output.mkdir(parents=True, exist_ok=False)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    hashes = dict(executable=sha(exe), shader=sha(shader))
    configurations = [("cube-normal", {}), ("cube-thick-oblique", dict(thickness=2, oblique=True)),
        ("sphere", dict(sphere=True))]
    report = dict(status="running", expectedCases=[name for name, _ in configurations], cases=[], hashes=hashes)
    write_json(output / "report.json", report)
    try:
        for name, config in configurations:
            images, captures, scenes, presets = [], [], [], []
            for quantity, mode in (("distance", 4), ("transmittance", 8), ("status", 5)):
                folder = output / (name + "-" + quantity)
                scene, preset = write_absorption_fixture(folder, ior=1.5, debug=mode, **config)
                scene["materials"][0]["roughness"] = .35
                write_json(folder / "scene.json", scene)
                evidence, image = capture_volume(exe, folder, 1, resource="CurrentFrameRadiance")
                scenes.append(scene)
                preset["pathTracing"].pop("debugOutput")
                presets.append(preset)
                images.append(image)
                captures.append(dict(quantity=quantity, **evidence))
            if scenes[0] != scenes[1] or scenes[0] != scenes[2] or presets[0] != presets[1] or presets[0] != presets[2]:
                raise ValueError("Paired paths use different inputs")
            report["cases"].append(dict(name=name, assessment=assess(*images), captures=captures))
            write_json(output / "report.json", report)
        if dict(executable=sha(exe), shader=sha(shader)) != hashes:
            raise ValueError("Binaries changed during capture")
        report["status"] = "passed-rough-volume-absorption"
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
