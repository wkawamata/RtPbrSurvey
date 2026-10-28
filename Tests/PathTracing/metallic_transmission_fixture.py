"""Generate constant and textured metallic/thin transmission comparisons."""

import argparse
import json
from pathlib import Path

from PIL import Image

from transmission_texture_fixture import BACKGROUND, write_fixture, write_json

METALLIC_RGBA = ((255, 0, 0, 64), (32, 255, 64, 0), (0, 32, 128, 255), (128, 64, 255, 32))


def write_metallic_fixture(folder, metallic=0, transmission=1, blue=None, varying=False,
                           backface=False, review=False, opaque=False):
    folder = Path(folder)
    scene = write_fixture(folder, rate=False, tint=False, review=review)
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    for index, value in enumerate((0, .5, 1) if review else (metallic,)):
        material = gltf["materials"][index]
        pbr = material["pbrMetallicRoughness"]
        pbr.update(baseColorFactor=[1, 1, 1, 1], metallicFactor=value, roughnessFactor=.25 if review else 0)
        pbr.pop("baseColorTexture", None)
        extension = material["extensions"]["KHR_materials_transmission"]
        extension["transmissionFactor"] = transmission
        extension.pop("transmissionTexture", None)
        material["name"] = f"Thin transmission metallic {value}"
        gltf["nodes"][index]["name"] = material["name"]
    if opaque:
        gltf["materials"][0].pop("extensions")
        for field in ("extensionsUsed", "extensionsRequired"):
            gltf[field] = [name for name in gltf[field] if name not in ("KHR_materials_transmission", "KHR_materials_ior")]
    if blue is not None or varying:
        colors = METALLIC_RGBA if varying else ((255, 0, blue, 0),) * 4
        image = Image.new("RGBA", (16, 16))
        image.putdata([colors[(y // 8) * 2 + x // 8] for y in range(16) for x in range(16)])
        image.save(folder / "metallic.png")
        gltf["textures"].append(dict(source=len(gltf["images"])))
        gltf["images"].append(dict(uri="metallic.png"))
        gltf["materials"][0]["pbrMetallicRoughness"]["metallicRoughnessTexture"] = dict(index=len(gltf["textures"]) - 1)
    if backface:
        gltf["nodes"][0]["rotation"] = [0, 1, 0, 0]
    write_json(folder / "panes.gltf", gltf)
    if review:
        scene.update(sceneId="metallic-thin-transmission", name="PT Metallic Thin Transmission",
                     description="目的: 金属度によって薄膜透過が減ることを確認。左からMetallic 0 / 0.5 / 1。\n"
                                 "全材質Transmission=1、Roughness=0.25。右は金属反射のみで格子は透過しない。\n"
                                 "制約: alpha mask、volume、色付き透過影は未対応。NRDはNative fallback。")
        write_json(folder / "scene.json", scene)
    else:
        preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
        preset["lighting"]["iblIntensity"] = 1
        preset["backBufferClearColor"] = [*BACKGROUND, 1]
        preset["pathTracing"].update(environmentEnabled=True, environmentSamplingMode=5)
        write_json(folder / "render-preset.json", preset)
    return scene


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", action="store_true")
    args = parser.parse_args()
    write_metallic_fixture(args.output, review=args.review)
