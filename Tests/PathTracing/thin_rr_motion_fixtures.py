"""Generate isolated motion controls from the smooth thin RR fixture."""
import argparse
import json
import math
from pathlib import Path

from thin_rr_smooth_fixture import generate as generate_smooth
from transmission_texture_fixture import write_json


def generate(output, pose_seconds=None):
    if pose_seconds is not None and (not math.isfinite(pose_seconds) or pose_seconds < 0):
        raise ValueError("Pose time must be finite and nonnegative")
    output.mkdir(parents=True, exist_ok=False)
    cases = (("camera-only", None), ("glass-only", "Clear Pane"), ("background-only", "Sphere 2"))
    for name, target in cases:
        folder = output / name
        generate_smooth(folder)
        path = folder / "scene.json"
        scene = json.loads(path.read_text(encoding="utf-8"))
        scene.update(sceneId=f"thin-rr-motion-{name}", name=f"PT Thin RR Motion: {name}",
                     description=f"Isolated motion control: {name}. Use the same fixed-step timeline for each backend.")
        scene.pop("turntable", None)
        if target is not None:
            node = next(n for n in scene["nodes"] if n["id"] == target)
            if node.get("parentId") is not None or not node.get("visible") or node["type"] != "primitive":
                raise ValueError(f"Invalid animation target: {target}")
            if any(n.get("parentId") == target for n in scene["nodes"]):
                raise ValueError(f"Animation target has children: {target}")
            if pose_seconds is None:
                scene["turntable"] = dict(nodeId=target, pivot=node["translation"], degreesPerSecond=0,
                                          translationAmplitude=[.2, 0, 0], translationPeriodSeconds=4)
            else:
                node["translation"][0] += .2 * math.sin(math.tau * (pose_seconds % 4) / 4)
        write_json(path, scene)
    write_json(output / "motion-plan.json", dict(fps=30, windowSize=[768, 432], poseSeconds=pose_seconds, cases=[
        dict(name=name, scene=f"{name}/scene.json", animatedNode=target,
             orbitDegreesPerSecond=3 if target is None else 0) for name, target in cases]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pose-seconds", type=float)
    args = parser.parse_args()
    generate(args.output.resolve(), args.pose_seconds)
