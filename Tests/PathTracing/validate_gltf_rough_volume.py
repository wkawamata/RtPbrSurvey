"""Paired-path absorption and roughness effect for a single convex glTF solid."""
import argparse
import copy
import json
from pathlib import Path

import numpy as np

from transmission_texture_fixture import ROOT, write_json
from validate_part1 import sha
from validate_rough_volume_absorption import assess
from volume_capture import capture_volume


def run(exe, output):
    output.mkdir(parents=True, exist_ok=False)
    source = ROOT / "Assets/Scene/PathTracingValidation/35-gltf-rough-volume-tetra"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    preset = json.loads((source / scene["renderPreset"]).read_text(encoding="utf-8"))
    gltf = json.loads((source / "volume.gltf").read_text(encoding="utf-8"))
    scene["assets"] = scene["assets"][:1]
    scene["nodes"] = scene["nodes"][:1]
    scene["renderPreset"] = "render-preset.json"
    preset["pathTracing"].update(environmentEnabled=False, accumulate=False, maxBounces=16)
    binaries = dict(executable=sha(exe), shader=sha(exe.with_name("shaders_PathTracing_CSMain.cso")))
    report = dict(status="running", expectedCases=["smooth", "rough"], cases=[], binaries=binaries,
        runnerSha256=sha(Path(__file__)), sourceSha256={name: sha(source/name) for name in ("scene.json", "volume.gltf")})
    write_json(output / "report.json", report)
    distances, statuses = [], []
    try:
        for name, roughness in (("smooth", 0), ("rough", .35)):
            material = copy.deepcopy(gltf)
            material["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = roughness
            images, evidence = [], []
            for quantity, mode in (("distance", 4), ("transmittance", 8), ("status", 5)):
                folder = output / (name + "-" + quantity)
                folder.mkdir()
                settings = copy.deepcopy(preset)
                settings["pathTracing"]["debugOutput"] = mode
                write_json(folder / "scene.json", scene)
                write_json(folder / "render-preset.json", settings)
                write_json(folder / "volume.gltf", material)
                capture, image = capture_volume(exe, folder, 1, resource="CurrentFrameRadiance")
                images.append(image)
                evidence.append(dict(quantity=quantity, **capture))
            if not np.isin(images[2][..., 0], [0, 3, 7]).all():
                raise ValueError("Medium/boundary error anywhere in the imported solid image")
            report["cases"].append(dict(name=name, roughness=roughness, assessment=assess(*images), captures=evidence))
            distances.append(images[0])
            statuses.append(images[2])
            write_json(output / "report.json", report)
        common = (statuses[0][..., 0] == 0) & (statuses[1][..., 0] == 0)
        common &= (distances[0][..., 0] == 1) & (distances[0][..., 1] == 1)
        common &= (distances[1][..., 0] == 1) & (distances[1][..., 1] == 1)
        if common.sum() < 512:
            raise ValueError("Insufficient common completed paths")
        difference = float(np.mean(np.abs(distances[0][..., 2][common]-distances[1][..., 2][common])))
        if difference <= .001:
            raise ValueError("Imported roughness did not change the traced paths")
        report["pathDifference"] = dict(commonPaths=int(common.sum()), meanAbsoluteDistanceDifference=difference)
        if dict(executable=sha(exe), shader=sha(exe.with_name("shaders_PathTracing_CSMain.cso"))) != binaries:
            raise ValueError("Binaries changed during capture")
        report["status"] = "passed-gltf-rough-volume"
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
