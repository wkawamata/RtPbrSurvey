"""Generate alpha coverage fixtures, kept separate from optical transmission."""

import argparse
import json
from pathlib import Path

from PIL import Image

from transmission_texture_fixture import write_fixture, write_json
from validate_emissive import fixture as write_emissive_fixture

ALPHAS = (0, 64, 128, 255)
PLATE = (.2, .4, .8)


def write_alpha_fixture(folder, factor=1, cutoff=.5, mode="MASK", texture=True, thin=False,
                        backface=False, double_sided=False, layer=None, review=False):
    folder = Path(folder)
    scene = write_fixture(folder, rate=False, tint=False, review=review)
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    image = Image.new("RGBA", (16, 16))
    image.putdata([(255, 255, 255, ALPHAS[(y // 8) * 2 + x // 8]) for y in range(16) for x in range(16)])
    image.save(folder / "alpha.png")
    gltf["textures"].append(dict(source=len(gltf["images"])))
    gltf["images"].append(dict(uri="alpha.png"))
    texture_index = len(gltf["textures"]) - 1
    for index in range(3 if review else 1):
        material = gltf["materials"][index]
        current_thin = index == 2 if review else thin
        current_mode = ("OPAQUE" if index == 0 else "MASK") if review else mode
        material.update(alphaMode=current_mode, alphaCutoff=cutoff, doubleSided=double_sided or review)
        pbr = material["pbrMetallicRoughness"]
        pbr.update(baseColorFactor=[*PLATE, factor], roughnessFactor=0, metallicFactor=0)
        pbr.pop("baseColorTexture", None)
        if texture:
            pbr["baseColorTexture"] = dict(index=texture_index)
        material["extensions"] = ({"KHR_materials_transmission": {"transmissionFactor": 1},
                                    "KHR_materials_ior": {"ior": 1}} if current_thin else {"KHR_materials_unlit": {}})
        material["name"] = current_mode + (" thin" if current_thin else " surface")
        gltf["nodes"][index]["name"] = material["name"]
    if backface:
        gltf["nodes"][0]["rotation"] = [0, 1, 0, 0]
    if layer:
        gltf["nodes"][0]["translation"] = [0, 0, -1 if layer == "transmission" else 8]
        if layer == "reflection":
            gltf["nodes"][0]["scale"] = [3, 3, 1]
            gltf["materials"][0]["doubleSided"] = True
            gltf["materials"][1]["doubleSided"] = True
            gltf["nodes"][1]["translation"] = [0, 0, 10]
        material_index = len(gltf["materials"])
        gltf["materials"].append(dict(pbrMetallicRoughness=dict(baseColorFactor=[1, 1, 1, 1],
            metallicFactor=1 if layer == "reflection" else 0, roughnessFactor=0)))
        if layer == "transmission":
            gltf["materials"][-1]["extensions"] = {"KHR_materials_transmission": {"transmissionFactor": 1},
                                                    "KHR_materials_ior": {"ior": 1}}
        gltf["meshes"].append(dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=1, TEXCOORD_0=2),
            indices=3, material=material_index)]))
        gltf["nodes"].append(dict(mesh=len(gltf["meshes"]) - 1))
        gltf["scenes"][0]["nodes"].append(len(gltf["nodes"]) - 1)
    write_json(folder / "panes.gltf", gltf)
    if review:
        scene.update(sceneId="alpha-mask-thin-transmission", name="PT Alpha MASK and Thin Transmission",
                     description="目的: alpha coverageと光学透過を区別して確認。左OPAQUE / 中央MASK / 右MASK+透過。\n"
                                 "Alphaは0/64/128/255。cutoff=0.5で下回る部分だけ表面を捨てる。\n"
                                 "右の残った部分は青く透過する。OPAQUEはtexture alphaを無視する。\n"
                                 "Native PT限定。BLEND、体積、色付き透過影は未対応。NRDはNative fallback。")
        write_json(folder / "scene.json", scene)
    return scene


def write_visibility_fixture(folder, variant, point=False, masked_emitter=False, emitter_backface=None):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    scene, preset = write_emissive_fixture(folder)
    write_alpha_fixture(folder / "blocker", texture=False, factor=0 if variant == "hole" else 1, double_sided=True)
    blocker_gltf = json.loads((folder / "blocker/panes.gltf").read_text(encoding="utf-8"))
    blocker_gltf["materials"] = blocker_gltf["materials"][:1]
    blocker_gltf["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][:3] = [0, 0, 0]
    blocker_gltf["meshes"] = blocker_gltf["meshes"][:1]
    blocker_gltf["nodes"] = blocker_gltf["nodes"][:1]
    blocker_gltf["scenes"][0]["nodes"] = [0]
    write_json(folder / "blocker/panes.gltf", blocker_gltf)
    scene["assets"].append(dict(id="blocker", type="gltf", path="blocker/panes.gltf"))
    scene["nodes"].append(dict(id="blocker", name="MASK blocker", parentId=None, type="gltf", assetId="blocker",
        translation=[0, 1.5, 0], rotation=[.7071067811865476, 0, 0, .7071067811865476],
        scale=[1, 1, 1], visible=variant != "clear"))
    preset["pathTracing"].update(pixelCenterPrimary=True, samplesPerFrame=1, randomSeed=7,
        nrdPreview=False, denoiserDistanceDiagnostics=True, emissiveSamplingMode=1)
    if point:
        scene["assets"] = [asset for asset in scene["assets"] if asset["id"] != "emitter"]
        scene["nodes"] = [node for node in scene["nodes"] if node["id"] != "emitter"]
        light = json.loads((Path(__file__).resolve().parents[2] /
            "Assets/Scene/PathTracingValidation/12-combined-lighting/render-preset.json").read_text(encoding="utf-8"))["lighting"]["lights"][0]
        light.update(color=[1, 1, 1], intensity=10, position=[0, 3, 0], range=100)
        preset["lighting"].update(lights=[light], directLightEnabled=True, emissiveEnabled=False)
        preset["pathTracing"].update(directLightingEnabled=True, emissiveEnabled=False, maxBounces=1)
    if masked_emitter:
        emitter = json.loads((folder / "emitter.gltf").read_text(encoding="utf-8"))
        emitter["materials"][0].update(alphaMode="MASK", alphaCutoff=.5)
        emitter["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][3] = 0
        write_json(folder / "emitter.gltf", emitter)
    if emitter_backface is not None:
        emitter = json.loads((folder / "emitter.gltf").read_text(encoding="utf-8"))
        emitter["materials"][0].update(alphaMode="MASK", alphaCutoff=.5, doubleSided=emitter_backface)
        write_json(folder / "emitter.gltf", emitter)
        node = next(node for node in scene["nodes"] if node["id"] == "emitter")
        node.update(rotation=[1, 0, 0, 0], translation=[0, 6, 0])
    write_json(folder / "scene.json", scene)
    write_json(folder / "preset.json", preset)
    return scene


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", action="store_true")
    args = parser.parse_args()
    write_alpha_fixture(args.output, review=args.review)
