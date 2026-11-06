"""Closed convex Volume fixtures for roughness channel/factor comparisons."""
import argparse
import base64
import json
from pathlib import Path
import struct

from PIL import Image

from transmission_texture_fixture import ROOT, write_json


def write_fixture(folder, factor=.7, green=128, textured=True):
    if not 0 <= factor <= 1 or not isinstance(green, int) or not 0 <= green <= 255:
        raise ValueError("Invalid roughness fixture parameters")
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    source = ROOT / "Assets/Scene/PathTracingValidation/35-gltf-rough-volume-tetra"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    gltf = json.loads((source / "volume.gltf").read_text(encoding="utf-8"))
    preset = json.loads((source / scene["renderPreset"]).read_text(encoding="utf-8"))
    scene["assets"] = scene["assets"][:1]
    scene["nodes"] = scene["nodes"][:1]
    scene.pop("turntable", None)
    scene["renderPreset"] = "render-preset.json"
    preset["pathTracing"].update(environmentEnabled=False, accumulate=False, maxBounces=16,
        pixelCenterPrimary=True, samplesPerFrame=1, debugOutput=6)
    pbr = gltf["materials"][0]["pbrMetallicRoughness"]
    pbr.update(metallicFactor=0, roughnessFactor=factor)
    if textured:
        blob = base64.b64decode(gltf["buffers"][0]["uri"].split(",", 1)[1])
        uv = struct.pack("<8f", 0, 0, 1, 0, 0, 1, 1, 1)
        view_index = len(gltf["bufferViews"])
        accessor_index = len(gltf["accessors"])
        gltf["bufferViews"].append(dict(buffer=0, byteOffset=len(blob), byteLength=len(uv)))
        gltf["accessors"].append(dict(bufferView=view_index, componentType=5126, count=4, type="VEC2"))
        gltf["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"] = accessor_index
        blob += uv
        gltf["buffers"][0] = dict(byteLength=len(blob), uri="data:application/octet-stream;base64,"+
            base64.b64encode(blob).decode("ascii"))
        gltf["images"] = [dict(uri="roughness.png")]
        gltf["textures"] = [dict(source=0)]
        pbr["metallicRoughnessTexture"] = dict(index=0)
        Image.new("RGBA", (4, 4), (32, green, 255, 0)).save(folder / "roughness.png")
    write_json(folder / "volume.gltf", gltf)
    write_json(folder / "scene.json", scene)
    write_json(folder / "render-preset.json", preset)
    return gltf


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write_fixture(args.output / "textured")
    write_fixture(args.output / "scalar", factor=.7 * 128 / 255, textured=False)
