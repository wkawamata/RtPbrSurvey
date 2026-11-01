"""Create a saved smooth/rough thin-sheet comparison without altering source models."""

import argparse
import json
from pathlib import Path

from transmission_texture_fixture import write_fixture, write_json


def write_rough_fixture(folder):
    folder = Path(folder)
    scene = write_fixture(folder, review=True)
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    for index, roughness in enumerate((0.0, 0.25, 0.6)):
        material = gltf["materials"][index]
        material["name"] = f"Thin sheet roughness {roughness}"
        material["pbrMetallicRoughness"].pop("baseColorTexture", None)
        material["pbrMetallicRoughness"]["roughnessFactor"] = roughness
        material["extensions"]["KHR_materials_transmission"].pop("transmissionTexture", None)
        gltf["nodes"][index]["name"] = material["name"]
    gltf.pop("images")
    gltf.pop("textures")
    write_json(folder / "panes.gltf", gltf)
    (folder / "rate.png").unlink()
    (folder / "tint.png").unlink()
    scene.update(sceneId="rough-thin-transmission", name="PT Rough Thin Transmission",
                 description="目的: 粗い薄膜透過のGGXローブを確認。左からRoughness 0 / 0.25 / 0.6。\n"
                             "確認: 左は格子が鮮明、中央と右は透過像・表面反射が広がる。\n"
                             "粗さは既存の材質Roughnessを使用。NRDはNative fallback。\n"
                             "制約: 体積屈折・吸収・alpha mask・透過の色付き影は未対応。\n"
                             "薄膜ローブはBSDF samplingのみ。単一散乱GGXなので高粗さでenergy lossがある。")
    write_json(folder / "scene.json", scene)
    return scene


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write_rough_fixture(args.output)
