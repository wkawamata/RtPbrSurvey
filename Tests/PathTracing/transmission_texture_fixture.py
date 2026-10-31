"""Small deterministic glTF fixtures for thin-transmission texture validation."""

import argparse
import base64
import copy
import json
from pathlib import Path
import struct

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RATE_RGBA = ((0, 255, 64, 255), (64, 200, 32, 0), (128, 32, 255, 64), (255, 0, 128, 0))
COLOR_RGBA = ((128, 64, 255, 0), (255, 128, 0, 64), (64, 255, 128, 128), (0, 64, 255, 255))
BACKGROUND = (0.8, 0.6, 0.4)


def write_json(path, value):
    text = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n")
    path.write_bytes(text.encode("utf-8"))


def write_fixture(folder, rate=True, tint=True, factor=1.0, ior=1.5, shared=False, review=False):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    for name, colors in (("rate.png", RATE_RGBA), ("tint.png", COLOR_RGBA)):
        image = Image.new("RGBA", (16, 16))
        image.putdata([colors[(y // 8) * 2 + x // 8] for y in range(16) for x in range(16)])
        image.save(folder / name)
    positions = (-1.4, -1.2, 0, 1.4, -1.2, 0, 1.4, 1.2, 0, -1.4, 1.2, 0)
    data = (struct.pack("<12f", *positions) + struct.pack("<12f", *([0, 0, 1] * 4))
            + struct.pack("<8f", 0, 0, 1, 0, 1, 1, 0, 1) + struct.pack("<6H", 0, 1, 2, 0, 2, 3))
    gltf = {
        "asset": {"version": "2.0", "generator": "RtPbrSurvey transmission texture fixture"},
        "extensionsUsed": ["KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_unlit"],
        "extensionsRequired": ["KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_unlit"],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode("ascii")}],
        "bufferViews": [{"buffer": 0, "byteOffset": offset, "byteLength": length}
                        for offset, length in ((0, 48), (48, 48), (96, 32), (128, 12))],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
                       "min": [-1.4, -1.2, 0], "max": [1.4, 1.2, 0]},
                      {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
                      {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
                      {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}],
        "images": [{"uri": "rate.png"}, {"uri": "tint.png"}],
        "textures": [{"source": 0}, {"source": 1}], "materials": [], "meshes": [], "nodes": [],
        "scenes": [{"nodes": []}], "scene": 0,
    }

    def add_pane(name, use_rate, use_tint, x=0, scale=(1, 1, 1)):
        material = {"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": [1, 1, 1, 1] if review else [0.6, 0.8, 1, 1],
            "metallicFactor": 0, "roughnessFactor": 0},
            "extensions": {"KHR_materials_transmission": {"transmissionFactor": factor},
                           "KHR_materials_ior": {"ior": ior}}}
        if use_rate:
            material["extensions"]["KHR_materials_transmission"]["transmissionTexture"] = {"index": 0}
        if use_tint:
            material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": 0 if shared else 1}
        index = len(gltf["materials"])
        gltf["materials"].append(material)
        gltf["meshes"].append({"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                "indices": 3, "material": index}]})
        gltf["nodes"].append({"name": name, "mesh": index, "translation": [x, 0, 0], "scale": list(scale)})

    if review:
        for x, name, use_rate, use_tint in ((-2.2, "Rate texture", True, False),
                                           (0, "Color texture", False, True), (2.2, "Combined textures", True, True)):
            add_pane(name, use_rate, use_tint, x, (1.9 / 2.8, 3.2 / 2.4, 1))
    else:
        add_pane("Texture test pane", rate, tint)
        index = len(gltf["materials"])
        gltf["materials"].append({"name": "Reference background", "extensions": {"KHR_materials_unlit": {}},
                                  "pbrMetallicRoughness": {"baseColorFactor": [*BACKGROUND, 1],
                                                           "metallicFactor": 0, "roughnessFactor": 1}})
        gltf["meshes"].append({"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                "indices": 3, "material": index}]})
        gltf["nodes"].append({"name": "Reference background", "mesh": index, "translation": [0, 0, -2], "scale": [4, 4, 1]})
    gltf["scenes"][0]["nodes"] = list(range(len(gltf["nodes"])))
    write_json(folder / "panes.gltf", gltf)
    source = ROOT / "Assets/Scene/PathTracingValidation/21-gltf-thin-transmission-suzanne"
    preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
    preset["pathTracing"].update(environmentEnabled=review, maxBounces=8, nrdPreview=False)
    if not review:
        preset["backBufferClearColor"] = [*BACKGROUND, 1]
    write_json(folder / "render-preset.json", preset)
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    scene.update(sceneId="thin-transmission-textures", name="PT Thin Transmission textures",
                 assets=[dict(id="panes", type="gltf", path="panes.gltf")], materials=[], nodes=[],
                 description="目的: 透過率R(linear)と透過色RGB(sRGB)を確認。左から透過率のみ／色のみ／両方。\n"
                             "確認: 背景格子に模様が掛かる。R=0は不透明であり穴ではない。RGBAのAは透過率に使わない。\n"
                             "制約: TEXCOORD_0、mip 0、既存共通sampler。体積・色付き影は未対応。")
    scene["camera"].update(position=[0, 0, -10 if review else -5], target=[0, 0, 0], verticalFovDegrees=40)

    def node(name, asset, position=(0, 0, 0), scale=(1, 1, 1)):
        return dict(id=name, name=name, parentId=None, type="gltf", assetId=asset,
                    translation=list(position), rotation=[0, 0, 0, 1], scale=list(scale), visible=True)

    scene["nodes"].append(node("Textured panes", "panes"))
    if review:
        scene["assets"].append(dict(id="checker", type="gltf", path="../20-thin-transmission-visual-comparison/checker.gltf"))
        scene["nodes"].append(node("Checker", "checker", (0, 0, 3), (3.6, 1.1, 1)))
        scene["materials"] = [dict(id="frame", name="Frame", baseColor=[0.03, 0.03, 0.03, 1], metallic=0, roughness=0.8)]
        for i, x in enumerate((-2.2, 0, 2.2)):
            for edge, position, scale in (("left", (x - 1.02, 0, 0), (0.1, 3.4, 0.12)),
                                          ("right", (x + 1.02, 0, 0), (0.1, 3.4, 0.12)),
                                          ("top", (x, 1.65, 0), (2.14, 0.1, 0.12)),
                                          ("bottom", (x, -1.65, 0), (2.14, 0.1, 0.12))):
                frame = node(f"Frame {i} {edge}", "", position, scale)
                frame.pop("assetId")
                frame.update(type="primitive", materialId="frame", primitive=dict(kind="cube", size=1))
                scene["nodes"].append(frame)
    write_json(folder / "scene.json", scene)
    return copy.deepcopy(scene)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", action="store_true")
    args = parser.parse_args()
    write_fixture(args.output, review=args.review)
