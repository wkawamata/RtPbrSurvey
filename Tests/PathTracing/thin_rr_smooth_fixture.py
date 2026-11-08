"""Generate a matched-framing smooth control for scene 20 RR evaluation."""
import argparse
import json
from pathlib import Path
import shutil

from transmission_texture_fixture import ROOT, write_json


def generate(output):
    source = ROOT / "Assets/Scene/PathTracingValidation/20-thin-transmission-visual-comparison"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    scene.update(sceneId="thin-rr-smooth-control", name="PT Thin Transmission Smooth Control")
    for material in scene["materials"]:
        transmission = material.get("transmission", {})
        if transmission.get("mode") == "thin" and transmission.get("factor", 0) > 0:
            material["roughness"] = 0
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(source / scene["renderPreset"], output / scene["renderPreset"])
    for asset in scene["assets"]:
        shutil.copyfile(source / asset["path"], output / asset["path"])
    write_json(output / "scene.json", scene)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    generate(parser.parse_args().output.resolve())
