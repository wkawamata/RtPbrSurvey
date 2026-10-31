"""Closed, smooth, clear primitive volumes for boundary-transport validation."""
import json
from pathlib import Path

from transmission_texture_fixture import ROOT, write_fixture, write_json


def write_volume_fixture(folder, mode=3, ior=1, sphere=False, bounces=16, tir=False):
    folder = Path(folder)
    scene = write_fixture(folder, rate=False, tint=False)
    gltf = json.loads((folder / "panes.gltf").read_text(encoding="utf-8"))
    gltf["materials"] = [gltf["materials"][1]]
    gltf["extensionsUsed"] = ["KHR_materials_unlit"]
    gltf["extensionsRequired"] = ["KHR_materials_unlit"]
    gltf["meshes"] = [gltf["meshes"][1]]
    gltf["meshes"][0]["primitives"][0]["material"] = 0
    gltf["nodes"] = [gltf["nodes"][1]]
    gltf["nodes"][0]["mesh"] = 0
    gltf["scenes"][0]["nodes"] = [0]
    write_json(folder / "panes.gltf", gltf)
    scene.update(sceneId="volume-boundary", name="PT Volume Boundary Contract",
        description="単一の閉じた無色Volume。境界の屈折・入出射・内部距離を確認。吸収・粗いVolumeは未対応。")
    scene["camera"].update(position=[0, 0, -5] if sphere else [2, 0, -5], target=[0, 0, 0])
    scene["materials"] = [dict(id="volume", name="Clear solid", baseColor=[1, 1, 1, 1], metallic=0, roughness=0,
        transmission=dict(mode="volume", factor=1, tint=[1, 1, 1], ior=ior,
            volume=dict(attenuationColor=[1, 1, 1], attenuationDistance=None)))]
    primitive = dict(kind="sphere", radius=1, stacks=64, slices=128) if sphere else dict(kind="cube", size=2)
    scene["nodes"].append(dict(id="volume", name="Clear volume", parentId=None, type="primitive", materialId="volume",
        primitive=primitive, translation=[0, 0, 0], rotation=[0, 0, 0, 1],
        scale=[1, 1, 1] if sphere else [3, 3, .5], visible=True))
    if tir:
        scene["camera"].update(position=[3, 0, -5], target=[0, 0, 2.68])
        scene["nodes"][-1]["scale"] = [.5, 3, 1.5]
    preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
    preset["pathTracing"].update(debugOutput=mode, maxBounces=bounces, pixelCenterPrimary=True, randomSeed=7)
    preset["shadow"].update(normalBias=.0001, rayTMin=.0001)
    write_json(folder / "scene.json", scene)
    write_json(folder / "render-preset.json", preset)
    return scene, preset


def write_volume_review(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    source = ROOT / "Assets/Scene/PathTracingValidation/21-gltf-thin-transmission-suzanne"
    scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
    scene.update(sceneId="volume-smooth-boundary", name="PT Smooth Volume Boundary",
        description="目的: 閉じた無色ガラスの境界屈折を確認。IOR=1.5、厚さ0.8の箱。\n"
                    "確認: 斜めから見た背景格子がガラス越しに横へずれる。薄膜の直進透過とは異なる。\n"
                    "DebugのVolume BoundariesはR=入射回数/G=出射回数/B=内部距離。\n"
                    "Volume StatusはR=異常コード/G=内部に残った状態/B=内部反射回数。\n"
                    "制約: 単一Cube/Sphere、カメラは外部、滑らかな無色材質のみ。吸収・粗いVolume・glTF Volumeは未対応。",
        assets=[dict(id="checker", type="gltf", path="../20-thin-transmission-visual-comparison/checker.gltf")],
        materials=[dict(id="volume", name="Clear volume", baseColor=[1, 1, 1, 1], metallic=0, roughness=0,
            transmission=dict(mode="volume", factor=1, tint=[1, 1, 1], ior=1.5,
                volume=dict(attenuationColor=[1, 1, 1], attenuationDistance=None)))],
        nodes=[dict(id="volume", name="Glass box", parentId=None, type="primitive", materialId="volume",
            primitive=dict(kind="cube", size=2), translation=[0, 0, 0], rotation=[0, 0, 0, 1], scale=[1.2, 1, .4], visible=True),
            dict(id="checker", name="Checker", parentId=None, type="gltf", assetId="checker",
            translation=[0, 0, 2], rotation=[0, 0, 0, 1], scale=[4, 2.5, 1], visible=True)])
    scene["camera"].update(position=[2, 1, -5], target=[0, 0, 0], verticalFovDegrees=40)
    preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
    preset["shadow"].update(normalBias=.0001, rayTMin=.0001)
    write_json(folder / "scene.json", scene)
    write_json(folder / "render-preset.json", preset)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", action="store_true")
    args = parser.parse_args()
    if args.review:
        write_volume_review(args.output)
    else:
        write_volume_fixture(args.output)
