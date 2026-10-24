"""Replay the real RR texture conversion pass; never call the RR SDK."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from contextlib import nullcontext

import numpy as np

from validate_inputs import read_buffer
from validate_denoiser_distances import capture
from validate_part1 import sha, write_json

RESOURCES = ("CurrentFrameRadiance", "NormalRoughness", "ViewZ", "MotionVectors",
             "Albedo", "MaterialF0Metallic", "DenoiserPrimaryPosition", "DenoiserSpecularDistance")
CHANNELS = (4, 4, 4, 4, 1, 1, 2, 1, 1)


def reference(inputs, inverse):
    color, normal, depth, motion, albedo, material, primary, distance = inputs
    height, width = depth.shape[:2]
    ys, xs = np.mgrid[:height, :width]
    ndc = np.stack((2*(xs+.5)/width-1, 1-2*(ys+.5)/height), axis=-1)
    near = np.concatenate((ndc, np.zeros((height, width, 1)), np.ones((height, width, 1))), axis=-1) @ inverse
    far = np.concatenate((ndc, np.ones((height, width, 2))), axis=-1) @ inverse
    with np.errstate(all="ignore"):
        view = near[..., :3]/near[..., 3:] - far[..., :3]/far[..., 3:]
        view /= np.linalg.norm(view, axis=-1, keepdims=True)
        length = np.linalg.norm(normal[..., :3], axis=-1)
        unit_normal = normal[..., :3]/length[..., None]
        n = np.abs(np.sum(unit_normal*view, axis=-1))
        a = normal[..., 3]**2
        bias = (.99044-1.28514*n+a*(1.29678-.755907*n)) / (
            1+2.92338*n+59.4188*n**3+a*(20.3225-27.0302*n+222.592*n**3)+
            a**3*(121.563+626.13*n+316.627*n**3))
        scale = (.0365463+3.32707*n+a*(9.0632-9.04756*n)) / (
            1+3.59685*n**2-1.36772*n**3+a*(9.04401-16.3174*n**2+9.22949*n**3)+
            a**3*(5.56589+19.7886*n**2-20.2123*n**3))
        specular = material[..., :3]*np.maximum(0, scale[..., None])+np.maximum(
            0, bias*np.clip(material[..., 1]*50, 0, 1))[..., None]
    finite = lambda v: np.isfinite(v).all(axis=-1)
    status = np.ones((height, width), dtype=np.uint32)
    valid = finite(primary) & (primary[..., 3] == 1) & (albedo[..., 3] == 1)
    status[valid] = 2
    valid &= finite(albedo) & finite(material) & (albedo[..., :3] >= 0).all(axis=-1) & (
        albedo[..., :3] <= 1).all(axis=-1) & (material >= 0).all(axis=-1) & (
        material <= 1).all(axis=-1) & np.isfinite(normal[..., 3]) & (
        normal[..., 3] >= 0) & (normal[..., 3] <= 1)
    status[valid] = 3
    valid &= finite(normal[..., :3]) & finite(view) & (np.abs(length-1) <= .002) & (
        np.abs(np.linalg.norm(view, axis=-1)-1) <= .002) & np.isfinite(depth[..., 0]) & (
        depth[..., 0] > 0) & finite(motion)
    status[valid] = 4
    valid &= finite(color[..., :3]) & (color[..., :3] >= 0).all(axis=-1)
    status[valid] = 5
    valid &= finite(distance) & (distance[..., 1] == 1) & (distance[..., 0] > 0) & (
        distance[..., 2] > 0) & (distance[..., 3] == 0)
    status[valid] = 2
    valid &= finite(specular) & (specular >= 0).all(axis=-1)
    status[valid] = 0
    outputs = [np.zeros((height, width, channels)) for channels in CHANNELS[:-1]]
    outputs[0][valid, :3] = color[valid, :3]
    outputs[1][valid, :3] = (albedo[..., :3]*(1-material[..., 3:]))[valid]
    outputs[2][valid, :3] = specular[valid]
    for slot in range(3):
        outputs[slot][valid, 3] = 1
    outputs[3][valid, :3] = unit_normal[valid]
    outputs[4][valid, 0] = normal[valid, 3]
    outputs[5][valid, 0] = depth[valid, 0]
    outputs[6][valid] = (motion*[.5, -.5])[valid]
    outputs[7][valid, 0] = distance[valid, 0]
    return outputs+[status[..., None]]


def synthetic(inverse):
    height, width = 9, 13  # Non-multiple of dispatch size, distinct row pitches.
    ys, xs = np.mgrid[:height, :width]
    fill = lambda value: np.broadcast_to(value, (height, width, len(value))).copy().astype(np.float64)
    inputs = [fill([.25, 3, 70000, 1]), fill([.6, 0, -.8, .55]), fill([12]),
              fill([.2, -.4]), fill([.7, .3, .1, 1]), fill([.271, .131, .061, .35]),
              fill([1, 2, 3, 1]), fill([2, 1, 1.999, 0])]
    inputs[0][..., 0] += xs*.01+ys*.1
    inputs[3][..., 0] += xs*.01
    inputs[1][..., 3] = (xs+ys)/(width+height-2)
    case = (ys*width+xs) % 12
    inputs[6][case == 6, 3] = 0
    inputs[5][case == 7, 0] = np.nan
    inputs[1][case == 8, :3] = 0
    inputs[0][case == 9, 0] = -1
    inputs[7][case == 10] = [0, 2, 100, 0]
    inputs[7][case == 11] = 0
    return inputs, inverse


def replay(args, folder, inputs, inverse):
    folder.mkdir(parents=True)
    height, width = inputs[0].shape[:2]
    # Match the actual float32 upload, including camera constants.
    inverse = np.asarray(inverse, dtype=np.float32).astype(np.float64)
    inputs = [np.asarray(v, dtype=np.float32).astype(np.float64) for v in inputs]
    (folder/"frame.bin").write_bytes(struct.pack("<16fII", *inverse.ravel(), width, height))
    for slot, values in enumerate(inputs):
        values.astype("<f4").tofile(folder/f"in{slot}.bin")
    command = [str(args.test_exe), str(args.shader), str(folder), str(folder/"gpu")]
    if args.hardware:
        command.append("--hardware")
    child = subprocess.run(command, capture_output=True, text=True, timeout=120)
    (folder/"gpu.log").write_text(child.stdout+child.stderr, encoding="utf-8")
    if child.returncode:
        raise RuntimeError("Texture replay failed: "+child.stdout+child.stderr)
    expected = reference(inputs, inverse)
    maximum = []
    for slot, target in enumerate(expected):
        dtype = "<u4" if slot == 8 else "<f4"
        observed = np.fromfile(folder/"gpu"/f"out{slot}.bin", dtype=dtype).reshape(target.shape)
        if slot == 8:
            matches = np.array_equal(observed, target)
        else:
            matches = np.isfinite(observed).all() and np.allclose(observed, target, atol=5e-5, rtol=5e-5)
        if not matches:
            raise ValueError(f"Texture replay mismatch in output slot {slot}")
        maximum.append(float(np.max(np.abs(observed.astype(float)-target))))
    counts = np.bincount(expected[8].ravel(), minlength=6).tolist()
    return dict(width=width, height=height, statusCounts=counts, maximumErrors=maximum,
                numericWholeFrameCandidate=counts[0] == width*height,
                nativeEvaluationQualified=False, d3d12Errors=0,
                outputs=[sha(folder/"gpu"/f"out{i}.bin") for i in range(9)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-exe", type=Path, required=True)
    parser.add_argument("--shader", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--hardware", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--exe", type=Path)
    args = parser.parse_args()
    args.test_exe = args.test_exe.resolve(strict=True)
    args.shader = args.shader.resolve(strict=True)
    if bool(args.source) != bool(args.exe):
        parser.error("Real capture requires both --source and --exe")
    scratch_context = nullcontext(None) if args.output else tempfile.TemporaryDirectory(
        prefix="rtpbr-rr-textures-", dir=args.test_exe.parent)
    with scratch_context as scratch:
        output = args.output.resolve() if args.output else Path(scratch)
        if output.exists() and any(output.iterdir()):
            parser.error("Output must be an empty directory")
        output.mkdir(parents=True, exist_ok=True)
        (output/"validation-script.py").write_bytes(Path(__file__).read_bytes())
        report = dict(status="running", records=[], failures=[], nativeEvaluationQualified=False,
                      testExeSha256=sha(args.test_exe), shaderSha256=sha(args.shader), scriptSha256=sha(Path(__file__)))
        try:
            projection = np.array([[1.3, 0, 0, 0], [0, 1.8, 0, 0], [.25, -.15, 1.01, 1], [0, 0, -.101, 0]])
            view = np.eye(4)
            view[3, :3] = [2, -3, 5]
            for name, inverse in (("orthographic", np.eye(4)), ("off-axis-perspective", np.linalg.inv(view @ projection)),
                                  ("invalid-camera", np.zeros((4, 4)))):
                inputs, inverse = synthetic(inverse)
                report["records"].append(dict(name=name, **replay(args, output/name, inputs, inverse)))
            if args.source:
                source = args.source.resolve(strict=True)
                args.exe = args.exe.resolve(strict=True)
                if output == source or source in output.parents or output in source.parents:
                    raise ValueError("Output must be separate from source")
                scene = json.loads((source/"scene.json").read_text())
                preset = json.loads((source/"preset.json").read_text())
                preset["pathTracing"].update(accumulate=True, samplesPerFrame=1, pixelCenterPrimary=True,
                                             denoiserDistanceDiagnostics=True)
                capture_folder = output/"captures"
                write_json(capture_folder/"scene.json", scene)
                write_json(capture_folder/"preset.json", preset)
                report.update(exeSha256=sha(args.exe), sceneSha256=sha(capture_folder/"scene.json"),
                              presetSha256=sha(capture_folder/"preset.json"), captures=[],
                              producerShaderSha256=sha(args.exe.parent/"shaders_PathTracing_CSMain.cso"))
                inputs, common = [], None
                for resource in RESOURCES:
                    (meta, values), record = capture(args.exe, capture_folder, resource,
                                                    capture_folder/"preset.json", resource)
                    report["captures"].append(record)
                    if meta["resource"] != "PathTracing."+resource:
                        raise ValueError("Wrong capture identity")
                    expected_format = 10 if resource in ("NormalRoughness", "Albedo", "MaterialF0Metallic") else (
                        41 if resource == "ViewZ" else 34 if resource == "MotionVectors" else 2)
                    if meta["format"] != expected_format:
                        raise ValueError("Wrong capture format: "+resource)
                    comparable = {k: v for k, v in meta.items() if k not in ("format", "resource")}
                    if common is not None and comparable != common:
                        raise ValueError("Capture cohort mismatch")
                    common = comparable
                    inputs.append(values)
                    write_json(output/"report.json", report)
                if common["primarySampling"] != "pixel-center" or common["samplesPerFrame"] != 1:
                    raise ValueError("Unsupported sampling profile")
                if common["matrixConvention"] != "transpose of DirectX row-vector matrices":
                    raise ValueError("Unknown matrix convention")
                record = replay(args, output/"captured-replay", inputs, np.asarray(common["inverseViewProjection"]).T)
                if record["statusCounts"][0] < 100 or sum(record["statusCounts"][1:]) == 0:
                    raise ValueError("Fixture must exercise candidates and fallback pixels")
                report["records"].append(dict(name="captured-replay", sampleStartIndex=common["sampleStartIndex"], **record))
            report["status"] = "passed"
        except Exception as error:
            report.update(status="failed", failures=[str(error)])
        report["limitation"] = "Standalone texture dispatch; engine scheduling, distance correlation and RR evaluate unqualified."
        write_json(output/"report.json", report)
        print(report["status"], [dict(name=r["name"], statusCounts=r["statusCounts"],
            maximumError=max(r["maximumErrors"])) for r in report["records"]], report["failures"])
        return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
