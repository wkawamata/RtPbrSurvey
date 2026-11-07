"""Generate tracked visual-review assets; refuses to overwrite user edits."""
import argparse
import json
from pathlib import Path

from transmission_texture_fixture import ROOT, write_json
from volume_roughness_texture_fixture import write_fixture


def generate(folder):
    write_fixture(folder, factor=.7, patterned=True)
    scene = json.loads((folder / "scene.json").read_text(encoding="utf-8"))
    source = ROOT / "Assets/Scene/PathTracingValidation/36-volume-observation-sphere"
    targets = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    scene.update(sceneId="volume-roughness-texture-review", name="PT Volume Roughness Texture Review",
        description="目的: glTF Volumeの粗さテクスチャを確認。G成分16/240の2領域、係数0.7。\n"
        "確認: 背景の色帯が比較的鮮明な部分と、粗くぼける部分が見える。\n"
        "Material ControlsのRoughnessを0へ下げると模様によるぼけが消え、0.7へ戻すと復帰する。\n"
        "IOR=1.5、吸収なし。単一凸Volume。粗い領域の暗さは単一散乱モデルの既知制約。\n"
        "厚み・部分透過テクスチャ、法線マップ、色付き透過照明の試験ではない。")
    scene["assets"] += targets["assets"]
    scene["nodes"] += targets["nodes"][1:]
    gltf = json.loads((folder / "volume.gltf").read_text(encoding="utf-8"))
    gltf["materials"][0]["extensions"]["KHR_materials_volume"].pop("attenuationDistance", None)
    preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
    preset["pathTracing"].update(debugOutput=3, accumulate=True, environmentEnabled=True,
                                  nrdPreview=False, maxBounces=16)
    write_json(folder / "scene.json", scene)
    write_json(folder / "volume.gltf", gltf)
    write_json(folder / "render-preset.json", preset)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.output.resolve())
