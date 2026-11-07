"""Whole-volume furnace controls and finite-bounce measurements (not full qualification)."""
import argparse
from pathlib import Path

import numpy as np

from transmission_texture_fixture import write_json
from validate_part1 import sha
from volume_boundary_fixture import write_volume_fixture
from volume_capture import capture_volume


def assess(image, matched):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128:
        raise ValueError("Invalid furnace dimensions")
    if not np.isfinite(image).all():
        raise ValueError("Nonfinite furnace evidence")
    h, w = image.shape[:2]
    patch = image[h//2-16:h//2+16, w//2-16:w//2+16, :3]
    mean = patch.mean(axis=(0, 1))
    if np.any(patch < -.0001) or np.any(patch > 1.002):
        raise ValueError("Lossless closed-volume furnace outside unit range")
    if matched and np.max(np.abs(patch - 1)) > .002:
        raise ValueError("Matched-IOR furnace must preserve unit radiance")
    return dict(mean=mean.tolist(), minimum=float(patch.min()), maximum=float(patch.max()),
                matchedIor=matched, roi=[w//2-16, h//2-16, w//2+16, h//2+16])


def summarize(cases):
    expected = {"matched-smooth", "matched-rough", "smooth-b4", "smooth-b8", "smooth-b16",
                "rough-b4", "rough-b8", "rough-b16"}
    names = [case["name"] for case in cases]
    if len(names) != len(expected) or set(names) != expected:
        raise ValueError("Missing or duplicate furnace cases")
    by_name = {case["name"]: np.asarray(case["assessment"]["mean"], dtype=float) for case in cases}
    if any(value.shape != (3,) or not np.isfinite(value).all() for value in by_name.values()):
        raise ValueError("Invalid furnace means")
    return {kind: dict(delta4to8=(by_name[kind+"-b8"] - by_name[kind+"-b4"]).tolist(),
                       delta8to16=(by_name[kind+"-b16"] - by_name[kind+"-b8"]).tolist(),
                       residualFromUnit=(1 - by_name[kind+"-b16"]).tolist())
            for kind in ("smooth", "rough")}


def assess_status(image):
    if image.ndim != 3 or image.shape[2] != 4 or min(image.shape[:2]) < 128:
        raise ValueError("Invalid status dimensions")
    if not np.isfinite(image).all():
        raise ValueError("Nonfinite status evidence")
    h, w = image.shape[:2]
    patch = image[h//2-16:h//2+16, w//2-16:w//2+16]
    codes = patch[..., 0]
    if not np.isin(codes, [0, 3, 7]).all():
        raise ValueError("Volume medium/boundary error or unknown status")
    return dict(normal=int((codes == 0).sum()), budgetTerminated=int((codes == 3).sum()),
                roughRejected=int((codes == 7).sum()), total=int(codes.size))


def run(exe, output, frames, window_size=None):
    if not 16 <= frames <= 256:
        raise ValueError("frames must be in [16,256]")
    output.mkdir(parents=True, exist_ok=False)
    shader = exe.with_name("shaders_PathTracing_CSMain.cso")
    hashes = dict(executable=sha(exe), shader=sha(shader), runner=sha(Path(__file__)))
    cases = [("matched-smooth", 1, 0, 4), ("matched-rough", 1, .7, 4),
             ("smooth-b4", 1.5, 0, 4), ("smooth-b8", 1.5, 0, 8), ("smooth-b16", 1.5, 0, 16),
             ("rough-b4", 1.5, .7, 4), ("rough-b8", 1.5, .7, 8), ("rough-b16", 1.5, .7, 16)]
    report = dict(status="running", hashes=hashes, frames=frames,
                  expectedCases=[c[0] for c in cases], windowSize=window_size, cases=[])
    write_json(output / "report.json", report)
    try:
        for name, ior, roughness, bounces in cases:
            folder = output / name
            scene, preset = write_volume_fixture(folder, mode=3, ior=ior, bounces=bounces)
            scene["nodes"] = [scene["nodes"][-1]]
            scene["assets"] = []
            scene["materials"][0]["roughness"] = roughness
            scene["camera"].update(position=[0, 0, -5], target=[0, 0, 0])
            preset["lighting"]["iblIntensity"] = 1
            preset["backBufferClearColor"] = [1, 1, 1, 1]
            preset["pathTracing"].update(environmentEnabled=True, environmentSamplingMode=2)
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", preset)
            evidence, image = capture_volume(exe, folder, frames, window_size=window_size)
            state = evidence["runtime"]
            if (state.get("environmentSamplingMode") != 2 or state.get("directLightingEnabled") is not False
                    or state.get("russianRoulette") is not False
                    or state.get("reconstructionOutput") != "native"):
                raise ValueError("Furnace lighting/transport runtime mismatch")
            assessment = assess(image, ior == 1)
            report["cases"].append(dict(name=name, ior=ior, roughness=roughness,
                bounces=bounces, assessment=assessment, capture=evidence))
            write_json(output / "report.json", report)
        if hashes["executable"] != sha(exe) or hashes["shader"] != sha(shader):
            raise ValueError("Binaries changed during furnace test")
        report["budgetComparison"] = summarize(report["cases"])
        report["status"] = "passed-controls-and-measured-finite-budgets"
        report["qualification"] = "Rough whole-volume convergence remains unqualified"
    except Exception as error:
        report.update(status="failed", failure=str(error))
        raise
    finally:
        write_json(output / "report.json", report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=64)
    parser.add_argument("--window-size", type=int, nargs=2)
    args = parser.parse_args()
    run(args.exe.resolve(strict=True), args.output.resolve(), args.frames, args.window_size)
