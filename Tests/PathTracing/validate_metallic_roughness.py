"""Qualify Native PT metallic/roughness guides against original texture samples."""
import argparse
import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from transmission_texture_fixture import write_fixture
from validate_inputs import read_buffer, projection_plane_forward
from validate_part1 import sha, write_json

ROOT = Path(__file__).resolve().parents[2]
HELMET = ROOT / "Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf"
SCENE = ROOT / "Assets/Scene/PathTracingValidation/32-helmet-reflection/scene.json"
CASES = ("textured", "red-alpha-control", "factor-only", "helmet")
RESOURCES = ("NormalRoughness", "MaterialF0Metallic", "ViewZ")
PIXELS = np.array([[[255, 64, 224, 0], [16, 192, 32, 255]],
                   [[80, 128, 160, 32], [200, 240, 80, 128]]], dtype=np.uint8)


def bilinear_repeat(pixels, uv):
    height, width = pixels.shape[:2]
    position = np.asarray(uv) * [width, height] - .5
    lower = np.floor(position).astype(np.int64)
    fraction = position - lower
    x, y = lower[:, 0], lower[:, 1]
    fx, fy = fraction[:, :1], fraction[:, 1:2]
    a = pixels[y % height, x % width] / 255.0
    b = pixels[y % height, (x + 1) % width] / 255.0
    c = pixels[(y + 1) % height, x % width] / 255.0
    d = pixels[(y + 1) % height, (x + 1) % width] / 255.0
    return ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy)


def node_matrix(node):
    if "matrix" in node:
        return np.asarray(node["matrix"], dtype=float).reshape(4, 4, order="F")
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    rotation = np.array([[1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w), 0],
                         [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w), 0],
                         [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y), 0],
                         [0, 0, 0, 1]], dtype=float)
    scale = np.diag([*node.get("scale", [1, 1, 1]), 1])
    result = rotation @ scale
    result[:3, 3] = node.get("translation", [0, 0, 0])
    return result


def reference_mesh(asset):
    """Read only the dense, single-node opaque fixtures qualified by this suite."""
    model = json.loads(asset.read_text(encoding="utf-8"))
    roots = model["scenes"][model.get("scene", 0)]["nodes"]
    if len(roots) != 1:
        raise ValueError("Reference requires exactly one root mesh node")
    node = model["nodes"][roots[0]]
    primitives = model["meshes"][node["mesh"]]["primitives"]
    if node.get("children") or len(primitives) != 1:
        raise ValueError("Reference excludes hierarchy and multiple primitives")
    primitive = primitives[0]
    if primitive.get("mode", 4) != 4:
        raise ValueError("Reference requires triangles")
    material = model["materials"][primitive["material"]]
    if material.get("alphaMode", "OPAQUE") != "OPAQUE" or material.get("extensions"):
        raise ValueError("Reference requires opaque core PBR")
    buffers = []
    for entry in model["buffers"]:
        uri = entry["uri"]
        buffers.append(base64.b64decode(uri.split(",", 1)[1]) if uri.startswith("data:")
                       else (asset.parent / uri).read_bytes())

    def accessor(index):
        info = model["accessors"][index]
        if info.get("sparse") or info.get("normalized"):
            raise ValueError("Reference excludes sparse or normalized accessors")
        view = model["bufferViews"][info["bufferView"]]
        dtype = np.dtype({5121: "u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[info["componentType"]])
        count = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[info["type"]]
        return np.ndarray((info["count"], count), dtype=dtype, buffer=buffers[view["buffer"]],
                          offset=view.get("byteOffset", 0) + info.get("byteOffset", 0),
                          strides=(view.get("byteStride", dtype.itemsize * count), dtype.itemsize)).copy()

    positions = accessor(primitive["attributes"]["POSITION"]).astype(float)
    uv = accessor(primitive["attributes"]["TEXCOORD_0"]).astype(float)
    indices = accessor(primitive["indices"]).reshape(-1, 3).astype(int)
    transform = node_matrix(node)
    positions = (np.column_stack((positions, np.ones(len(positions)))) @ transform.T)[:, :3]
    positions[:, 2] *= -1
    if np.linalg.det(transform[:3, :3]) > 0:
        indices = indices[:, [0, 2, 1]]
    pbr = material["pbrMetallicRoughness"]
    texture = None
    if "metallicRoughnessTexture" in pbr:
        info = pbr["metallicRoughnessTexture"]
        if info.get("texCoord", 0) != 0 or info.get("extensions"):
            raise ValueError("Reference requires untransformed TEXCOORD_0")
        image = model["images"][model["textures"][info["index"]]["source"]]
        texture = np.asarray(Image.open(asset.parent / image["uri"]).convert("RGBA"))
    return positions[indices], uv[indices], pbr, texture


def closest_hit(triangles, origin, direction, tmin, tmax):
    edge1, edge2 = triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    p = np.cross(direction, edge2)
    determinant = np.einsum("ij,ij->i", edge1, p)
    valid = determinant > 1e-12
    inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=valid)
    offset = origin - triangles[:, 0]
    u = np.einsum("ij,ij->i", offset, p) * inverse
    q = np.cross(offset, edge1)
    v = q @ direction * inverse
    distance = np.einsum("ij,ij->i", edge2, q) * inverse
    valid &= (u >= 0) & (v >= 0) & (u + v <= 1) & (distance >= tmin) & (distance <= tmax)
    if not valid.any():
        return None
    index = int(np.argmin(np.where(valid, distance, np.inf)))
    return index, np.array([1 - u[index] - v[index], u[index], v[index]]), float(distance[index])


def pixel_ray(meta, x, y):
    ndc = [2 * (x + .5) / meta["width"] - 1, 1 - 2 * (y + .5) / meta["height"]]
    inverse = np.asarray(meta["inverseViewProjection"]).T
    near = np.array([*ndc, 0, 1]) @ inverse
    far = np.array([*ndc, 1, 1]) @ inverse
    origin = near[:3] / near[3]
    direction = far[:3] / far[3] - origin
    return origin, direction / np.linalg.norm(direction)


def make_case(folder, name):
    if name == "helmet":
        folder.mkdir(parents=True)
        scene = json.loads(SCENE.read_text(encoding="utf-8"))
        preset = json.loads(SCENE.with_name("render-preset.json").read_text(encoding="utf-8"))
        scene["assets"][0]["path"] = Path(os.path.relpath(HELMET, folder)).as_posix()
        asset = HELMET
    else:
        scene = write_fixture(folder)
        asset = folder / "panes.gltf"
        model = json.loads(asset.read_text(encoding="utf-8"))
        for key in ("extensionsUsed", "extensionsRequired"):
            model.pop(key, None)
        pbr = dict(baseColorFactor=[.3, .5, .7, 1], metallicFactor=.7, roughnessFactor=.6)
        if name != "factor-only":
            pbr["metallicRoughnessTexture"] = dict(index=0)
        model.update(materials=[dict(name="MR control", pbrMetallicRoughness=pbr)],
                     meshes=model["meshes"][:1], nodes=model["nodes"][:1],
                     images=[dict(uri="mr.png")], textures=[dict(source=0)], scenes=[dict(nodes=[0])])
        colors = PIXELS.copy()
        if name == "red-alpha-control":
            colors[:, :, [0, 3]] = 255 - colors[:, :, [0, 3]]
        Image.fromarray(colors).save(folder / "mr.png")
        write_json(asset, model)
        scene["name"] = "PT metallic roughness control"
        preset = json.loads((folder / "render-preset.json").read_text(encoding="utf-8"))
    scene["environment"]["iblEnabled"] = False
    preset["lighting"].update(lights=[], directLightEnabled=False, emissiveEnabled=False,
                               iblIntensity=0, skyboxEnabled=False)
    preset["pathTracing"].update(accumulate=True, maxBounces=1, samplesPerFrame=1, randomSeed=7,
                                  directLightingEnabled=False, environmentEnabled=False, emissiveEnabled=False,
                                  pixelCenterPrimary=True, denoiserDistanceDiagnostics=False, debugOutput=3,
                                  russianRouletteEnabled=False, nrdPreview=False)
    write_json(folder / "scene.json", scene)
    write_json(folder / "render-preset.json", preset)
    return scene, asset


def capture(exe, folder, scene_name, resource, timeout):
    path, log = folder / (resource + ".ptbuf"), folder / (resource + ".log")
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-EnablePathTracing",
               "-PathTracingReconstruction", "native", "-WindowSize", "640", "360", "-PathTracingSamples", "1",
               "-DebugPreviewResource", "PathTracing." + resource, "-CapturePath", str(path),
               "-LogToFile", str(log), "-ExitAfterCapture"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    with (folder / (resource + "-process.log")).open("wb") as stream:
        child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise ValueError("Capture timed out: " + resource)
    text = log.read_text(encoding="utf-8-sig")
    states = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
              if line.startswith("[PathTracing] ")]
    if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(states) != 1:
        raise ValueError("Runtime/Debug Layer failure: " + resource)
    state = states[0]
    expected = dict(scene=scene_name, reconstructionRequested="native", reconstructionOutput="native",
                    accumulatedSamples=1, randomSeed=7, maxBounces=1, pixelCenterPrimary=True,
                    samplesPerFrame=1, environmentEnabled=False, directLightingEnabled=False,
                    emissiveEnabled=False, d3d12DebugLayerEnabledByApp=True)
    if any(type(state.get(k)) is not type(v) or state[k] != v for k, v in expected.items()):
        raise ValueError("Runtime settings mismatch")
    meta, values = read_buffer(path)
    if (meta.get("resource") != "PathTracing." + resource or meta.get("primarySampling") != "pixel-center"
            or meta.get("sampleStartIndex") != 1 or meta.get("width") != 640 or meta.get("height") != 360):
        raise ValueError("Capture metadata mismatch")
    return dict(command=command, runtime=state, metadata=meta, captureSha256=sha(path), logSha256=sha(log)), values


def assess(asset, captures, name):
    triangles, uv_triangles, pbr, texture = reference_mesh(asset)
    meta = captures["ViewZ"][0]["metadata"]
    normal = captures["NormalRoughness"][1]
    material = captures["MaterialF0Metallic"][1]
    depth = captures["ViewZ"][1]
    forward = projection_plane_forward(meta["inverseViewProjection"])
    camera = np.asarray(meta["cameraPosition"])
    samples = []
    for y in range(12, meta["height"] - 12, 12):
        for x in range(12, meta["width"] - 12, 12):
            origin, direction = pixel_ray(meta, x, y)
            hit = closest_hit(triangles, origin, direction, meta["rayTMin"], meta["rayTMax"])
            gpu_hit = np.linalg.norm(normal[y, x, :3]) > .5
            if (hit is not None) != gpu_hit:
                raise ValueError("CPU/GPU hit visibility mismatch at " + str((x, y)))
            if hit is None:
                continue
            index, barycentric, distance = hit
            uv = barycentric @ uv_triangles[index]
            expected_depth = float((origin + direction * distance - camera) @ forward)
            samples.append(dict(pixel=[x, y], uv=uv.tolist(), expectedDepth=expected_depth,
                                observedDepth=float(depth[y, x, 0]),
                                observedMetallic=float(material[y, x, 3]),
                                observedRoughness=float(normal[y, x, 3])))
    if len(samples) < 100:
        raise ValueError("Insufficient independently matched material pixels")
    sampled = np.ones((len(samples), 4)) if texture is None else bilinear_repeat(
        texture, np.asarray([sample["uv"] for sample in samples]))
    expected_metal = sampled[:, 2] * pbr.get("metallicFactor", 1)
    expected_rough = sampled[:, 1] * pbr.get("roughnessFactor", 1)
    observed_metal = np.asarray([s["observedMetallic"] for s in samples])
    observed_rough = np.asarray([s["observedRoughness"] for s in samples])
    tolerance = .004 + 4 / 255 if name == "helmet" else .004
    checks = dict(metallicMaxAbsolute=float(np.abs(expected_metal - observed_metal).max()),
                  roughnessMaxAbsolute=float(np.abs(expected_rough - observed_rough).max()),
                  depthMaxAbsolute=max(abs(s["expectedDepth"] - s["observedDepth"]) for s in samples),
                  materialTolerance=tolerance, depthTolerance=.002)
    if name != "helmet":
        pixels = np.asarray([s["pixel"] for s in samples])
        rgb = material[pixels[:, 1], pixels[:, 0], :3]
        expected_f0 = .04 * (1 - expected_metal[:, None]) + np.asarray(pbr["baseColorFactor"][:3]) * expected_metal[:, None]
        checks["f0MaxAbsolute"] = float(np.abs(rgb - expected_f0).max())
    for i, sample in enumerate(samples):
        sample.update(expectedMetallic=float(expected_metal[i]), expectedRoughness=float(expected_rough[i]))
    checks["passed"] = (checks["metallicMaxAbsolute"] <= tolerance and checks["roughnessMaxAbsolute"] <= tolerance
                        and checks["depthMaxAbsolute"] <= .002 and checks.get("f0MaxAbsolute", 0) <= tolerance)
    result = dict(sampleCount=len(samples), checks=checks, samples=samples)
    if name == "helmet":
        region = material[145:195, 280:360, 3]
        roughness = normal[145:195, 280:360, 3]
        if not np.all(np.linalg.norm(normal[145:195, 280:360, :3], axis=2) > .5):
            raise ValueError("Helmet front-center ROI does not cover the model")
        result["frontCenterRoi"] = dict(rect=[280, 145, 80, 50],
            metallicPercentiles=np.percentile(region, [0, 10, 50, 90, 100]).tolist(),
            roughnessPercentiles=np.percentile(roughness, [0, 10, 50, 90, 100]).tolist())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "bin/x64/Debug/RtPbrSurvey.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "bin/PathTracingValidation/metallic-roughness")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("Timeout must be positive")
    exe, output = args.exe.resolve(strict=True), args.output.resolve()
    if output.exists():
        parser.error("Use a new output directory; old evidence is never overwritten")
    output.mkdir(parents=True)
    report = dict(schemaVersion=1, status="running", generatedUtc=datetime.now(timezone.utc).isoformat(),
        testedCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        workingTreeModified=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        executableSha256=sha(exe), expectedCases=list(CASES), expectedResources=list(RESOURCES), records=[],
        sourceSha256={p: sha(ROOT / p) for p in ("Shaders/SceneRayQuery.hlsli", "Shaders/shaders_PathTracing.hlsl",
            "GltfLoader.cpp", "Engine/RtPbrSurveyEngine.cpp", "Tests/PathTracing/validate_metallic_roughness.py")},
        scope="Core opaque metallic/roughness inputs; not reflection convergence or RR quality")
    write_json(output / "report.json", report)
    images = {}
    try:
        for name in CASES:
            folder = output / name
            scene, asset = make_case(folder, name)
            model = json.loads(asset.read_text(encoding="utf-8"))
            inputs = [folder / "scene.json", folder / "render-preset.json", asset]
            inputs += [asset.parent / entry["uri"] for entry in model.get("buffers", []) + model.get("images", [])
                       if not entry["uri"].startswith("data:")]
            record = dict(name=name, inputSha256={str(p.resolve()): sha(p) for p in inputs}, captures={})
            report["records"].append(record)
            captures = {}
            for resource in RESOURCES:
                evidence, image = capture(exe, folder, scene["name"], resource, args.timeout)
                record["captures"][resource] = evidence
                captures[resource] = evidence, image
                write_json(output / "report.json", report)
            record["assessment"] = assess(asset, captures, name)
            if not record["assessment"]["checks"]["passed"]:
                raise ValueError("Material assessment failed: " + name)
            images[name] = {resource: captures[resource][1] for resource in RESOURCES}
            print(name + ": passed", flush=True)
            write_json(output / "report.json", report)
        report["redAlphaIgnored"] = all(np.array_equal(images["textured"][r], images["red-alpha-control"][r])
                                        for r in RESOURCES)
        if not report["redAlphaIgnored"]:
            raise ValueError("Red or alpha changed metallic/roughness guides")
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", failure=str(error))
    write_json(output / "report.json", report)
    print(json.dumps({k: report[k] for k in ("status", "redAlphaIgnored", "failure") if k in report}))
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
