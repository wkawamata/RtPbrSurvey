"""Generate tracked single-volume observation scenes with shared color targets."""
import base64
import copy
import json
from pathlib import Path
import struct

from transmission_texture_fixture import ROOT, write_json


def mesh_asset(positions, primitives, materials):
    binary = struct.pack("<" + "f" * len(positions), *positions)
    views = [dict(buffer=0, byteOffset=0, byteLength=len(binary))]
    accessors = [dict(bufferView=0, componentType=5126, count=len(positions)//3, type="VEC3",
        min=[min(positions[i::3]) for i in range(3)], max=[max(positions[i::3]) for i in range(3)])]
    meshes = []
    for indices, material in primitives:
        data = struct.pack("<" + "H" * len(indices), *indices)
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(data)))
        binary += data
        accessors.append(dict(bufferView=len(views)-1, componentType=5123, count=len(indices), type="SCALAR"))
        meshes.append(dict(attributes=dict(POSITION=0), indices=len(accessors)-1, material=material))
    return dict(asset=dict(version="2.0", generator="RtPbrSurvey observation fixture"), scene=0,
        scenes=[dict(nodes=[0])], nodes=[dict(name="Target", mesh=0)],
        buffers=[dict(byteLength=len(binary), uri="data:application/octet-stream;base64," + base64.b64encode(binary).decode())],
        bufferViews=views, accessors=accessors, meshes=[dict(primitives=meshes)], materials=materials)


def generate():
    root = ROOT / "Assets/Scene/PathTracingValidation"
    shared = root / "36-volume-observation-sphere"
    shared.mkdir(exist_ok=True)
    colors = [(1, .15, .05), (1, .65, .05), (.95, .95, .95), (.05, .7, .2),
        (.04, .2, 1), (.7, .1, .8), (.95, .95, .95), (.03, .03, .03)]
    positions, primitives, materials = [], [], []
    for i, color in enumerate(colors):
        left, right = -4 + i, -3 + i
        start = len(positions)//3
        positions += [left,-2.5,0, right,-2.5,0, right,2.5,0, left,2.5,0]
        primitives.append(([start,start+1,start+2,start,start+2,start+3], i))
        materials.append(dict(name="Band " + str(i+1), doubleSided=True,
            pbrMetallicRoughness=dict(baseColorFactor=[*color,1], metallicFactor=0, roughnessFactor=1),
            extensions={"KHR_materials_unlit": {}}))
    target = mesh_asset(positions, primitives, materials)
    target.update(extensionsUsed=["KHR_materials_unlit"], extensionsRequired=["KHR_materials_unlit"])
    write_json(shared / "color-target.gltf", target)
    glass = dict(name="Observation glass", pbrMetallicRoughness=dict(metallicFactor=0, roughnessFactor=0),
        extensions={"KHR_materials_transmission": dict(transmissionFactor=1),
            "KHR_materials_volume": dict(thicknessFactor=1, attenuationColor=[.25,.5,.8])})
    prism = mesh_asset([-1,-.75,-.5, 1,-.75,-.5, 0,.95,-.5, -1,-.75,.5, 1,-.75,.5, 0,.95,.5],
        [([0,2,1,3,4,5,0,1,4,0,4,3,1,2,5,1,5,4,2,0,3,2,3,5], 0)], [glass])
    prism.update(extensionsUsed=["KHR_materials_transmission","KHR_materials_volume"],
        extensionsRequired=["KHR_materials_transmission","KHR_materials_volume"])
    base = json.loads((root / "35-gltf-rough-volume-tetra/scene.json").read_text(encoding="utf-8"))
    base.pop("turntable", None)
    preset = json.loads((root / "33-rough-volume-cube/render-preset.json").read_text(encoding="utf-8"))
    preset["pathTracing"].update(environmentEnabled=True, environmentSamplingMode=5, maxBounces=16)
    for number, shape in ((36,"sphere"), (37,"slab"), (38,"prism")):
        folder = root / f"{number}-volume-observation-{shape}"
        folder.mkdir(exist_ok=True)
        scene = copy.deepcopy(base)
        scene.update(sceneId="volume-observation-"+shape, name="PT Volume Observation " + shape.title(),
            renderPreset="render-preset.json",
            description="目的: 形状ごとの反射・屈折を同一条件で比較。初期IOR=1.5、粗さ0、吸収なし。\n"
                "背面の色帯は屈折像、左右の色帯パネルは反射像の識別用。\n"
                "確認: IOR=1で形状が消える。IOR=1.5へ戻し、像のずれ・変形を観察。\n"
                "次に粗さを増やしてぼけを確認。InfiniteをOFFにして吸収色・距離を確認。\n"
                "単一Volumeの比較用。色付き透過影・コースティクス・複合媒体の受入試験ではない。")
        scene["camera"].update(position=[2.8,1.5,-6], target=[0,0,0], verticalFovDegrees=40)
        scene["assets"] = [dict(id="target", type="gltf", path="../36-volume-observation-sphere/color-target.gltf")]
        panels = []
        for name, pos, rot, scale in (("Back target", [0,0,3], [0,0,0,1], [1,1,1]),
            ("Left reflection", [-3,0,-1], [0,.7071067811865476,0,.7071067811865476], [.6,.8,1]),
            ("Right reflection", [3,0,-1], [0,-.7071067811865476,0,.7071067811865476], [.6,.8,1])):
            panels.append(dict(id=name.replace(" ","-"), name=name, type="gltf", assetId="target",
                parentId=None, translation=pos, rotation=rot, scale=scale, visible=True))
        if shape == "prism":
            write_json(folder / "volume.gltf", prism)
            scene["assets"].append(dict(id="solid", type="gltf", path="volume.gltf"))
            solid = dict(id="solid", name="Glass prism", type="gltf", assetId="solid")
            scene["materials"] = []
        else:
            scene["materials"] = [dict(id="glass", name="Observation glass", baseColor=[1,1,1,1], metallic=0, roughness=0,
                transmission=dict(mode="volume", factor=1, tint=[1,1,1], ior=1.5,
                    volume=dict(attenuationColor=[.25,.5,.8], attenuationDistance=None)))]
            primitive = dict(kind="sphere", radius=1, stacks=64, slices=128) if shape == "sphere" else dict(kind="cube", size=2)
            solid = dict(id="solid", name="Glass "+shape, type="primitive", materialId="glass", primitive=primitive)
        solid.update(parentId=None, translation=[0,0,0], rotation=[0,0,0,1],
            scale=[1,.9,.35] if shape == "slab" else [1,1,1], visible=True)
        scene["nodes"] = [solid, *panels]
        write_json(folder / "render-preset.json", preset)
        write_json(folder / "scene.json", scene)


if __name__ == "__main__":
    generate()
