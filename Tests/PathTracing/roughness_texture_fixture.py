"""Generate linear-G roughness fixtures with deliberately unrelated R/B/A values."""

import argparse
import json
from pathlib import Path

from PIL import Image

from rough_transmission_fixture import write_rough_fixture
from transmission_texture_fixture import write_json
from validate_rough_transmission import write_furnace_fixture

ROUGHNESS_RGBA = ((255, 0, 128, 64), (32, 64, 255, 0), (0, 128, 32, 255), (128, 255, 0, 32))


def add_roughness_texture(folder, gltf, factors, varying=True):
    colors = ROUGHNESS_RGBA if varying else ((32, 128, 255, 0),) * 4
    image = Image.new("RGBA", (16, 16))
    image.putdata([colors[(y // 8) * 2 + x // 8] for y in range(16) for x in range(16)])
    image.save(folder / "roughness.png")
    images = gltf.setdefault("images", [])
    textures = gltf.setdefault("textures", [])
    textures.append(dict(source=len(images)))
    images.append(dict(uri="roughness.png"))
    for index, factor in enumerate(factors):
        pbr = gltf["materials"][index]["pbrMetallicRoughness"]
        pbr["roughnessFactor"] = factor
        pbr["metallicRoughnessTexture"] = dict(index=len(textures) - 1)
    write_json(folder / "panes.gltf", gltf)


def write_roughness_texture_fixture(folder, factor=1.0, varying=False, review=False):
    folder = Path(folder)
    if review:
        scene = write_rough_fixture(folder)
    else:
        write_furnace_fixture(folder, factor, 5)
        scene = json.loads((folder / "scene.json").read_text(encoding="utf-8"))
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    add_roughness_texture(folder, gltf, (1, .5) if review else (factor,), varying=review or varying)
    if review:
        gltf["materials"][2]["pbrMetallicRoughness"]["roughnessFactor"] = .5
        for index, name in enumerate(("Roughness texture G x 1.0", "Roughness texture G x 0.5", "Constant roughness 0.5")):
            gltf["materials"][index]["name"] = name
            gltf["nodes"][index]["name"] = name
        write_json(folder / "panes.gltf", gltf)
        scene.update(sceneId="roughness-textured-thin-transmission", name="PT Roughness Textured Thin Transmission",
                     description="目的: metallic-roughness textureのG(linear)が薄膜透過の粗さに反映されることを確認。\n"
                                 "左: G x 1.0 / 中央: G x 0.5 / 右: 定数Roughness 0.5。\n"
                                 "Gは0/64/128/255の4領域。R/B/Aは別の値で、粗さに使用しない。\n"
                                 "全材質metallicFactor=0。volume、alpha mask、色付き透過影は未対応。")
        write_json(folder / "scene.json", scene)
    return scene


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", action="store_true")
    args = parser.parse_args()
    write_roughness_texture_fixture(args.output, review=args.review)
