"""Absorption scenes with white boundaries and explicit world-space lengths."""
import json
from pathlib import Path

from transmission_texture_fixture import write_json
from volume_boundary_fixture import write_volume_fixture, write_volume_review

COLOR = (.25, .5, .8)


def write_absorption_fixture(folder, color=COLOR, distance=1, thickness=1, oblique=False,
        sphere=False, ior=1, debug=3, bias=.0001, uniform_scale=1, tir=False):
    folder = Path(folder)
    scene, preset = write_volume_fixture(folder, mode=debug, ior=ior, sphere=sphere, tir=tir)
    scene.update(sceneId="volume-absorption", name="PT Volume Absorption Contract",
        description="境界色は白。色付き減衰は内部の実距離だけで決まる。")
    volume = scene["materials"][0]["transmission"]["volume"]
    volume.update(attenuationColor=list(color), attenuationDistance=distance)
    if not tir:
        scene["camera"].update(position=[2 if oblique else 0, 0, -5], target=[0, 0, 0])
        if not sphere:
            scene["nodes"][-1]["scale"] = [3*uniform_scale, 3*uniform_scale, thickness*.5*uniform_scale]
    preset["shadow"]["normalBias"] = bias
    write_json(folder/"scene.json", scene)
    write_json(folder/"render-preset.json", preset)
    return scene, preset


def write_absorption_review(folder, thickness=.8, sphere=False):
    folder = Path(folder)
    write_volume_review(folder)
    scene = json.loads((folder/"scene.json").read_text(encoding="utf-8"))
    scene.update(sceneId="volume-absorption-sphere" if sphere else f"volume-absorption-{thickness}",
        name="PT Colored Volume Sphere" if sphere else f"PT Volume Absorption Thickness {thickness}",
        description="目的: 境界色ではなく内部距離で色が変わるVolume吸収を確認。\n"
                    "IOR=1.5、吸収色RGB=(0.25,0.5,0.8)、減衰距離1 world unit。\n"
                    "確認: 長い距離を通る部分ほど暗く着色する。厚さ0.8と1.6の箱を別シーンで比較できる。\n"
                    "Volume Transmittanceは各区間の吸収だけを積算。Fresnelや背景色は含まない。\n"
                    "制約: 単一Cube/Sphere、カメラ外側、滑らかな境界のみ。色付き透過影・粗いVolume・glTF Volumeは未対応。")
    scene["materials"][0]["transmission"]["volume"].update(attenuationColor=list(COLOR), attenuationDistance=1)
    node = scene["nodes"][0]
    if sphere:
        node.update(name="Colored glass sphere", primitive=dict(kind="sphere", radius=1, stacks=64, slices=128), scale=[1, 1, 1])
    else:
        node["scale"][2] = thickness/2
    write_json(folder/"scene.json", scene)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--thickness", type=float, default=.8)
    parser.add_argument("--sphere", action="store_true")
    args = parser.parse_args()
    write_absorption_review(args.output, args.thickness, args.sphere)
