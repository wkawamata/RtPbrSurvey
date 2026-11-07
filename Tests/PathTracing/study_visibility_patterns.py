"""GPU-only feasibility study; isolated shader variants, not production integration."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from capture_pt_hybrid_comparison import ROOT
from validate_part1 import sha, write_json


SHADERS = ROOT / "Shaders"
HERE = Path(__file__).resolve().parent
CSO = "shaders_DiffuseEnvironmentVisibility_CSMain.cso"
PSO = "shaders_LightPass_PSMain.cso"


def compile_shader(dxc, source, target, profile, entry, definitions):
    command = [str(dxc), "-nologo", "-T" + profile, "-E" + entry, "-O3",
               "-I", str(SHADERS), "-I", str(HERE), "-Fo", str(target), str(source)]
    command += ["-D" + key + "=" + str(value) for key, value in definitions.items()]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def capture(exe, folder, decoder, size, mask, warmup=3):
    log = folder / "debug.log"
    command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", str(size), str(size),
               "-LogToFile", str(log), "-ExitAfterCapture"]
    if mask:
        command += ["-CaptureSessionOutputDir", str(folder), "-CaptureSessionBaseName", "mask",
                    "-CaptureSessionFormat", "exr", "-CaptureSessionFrames", "1", "-CaptureSessionWarmupFrames", "2"]
    else:
        command += ["-CapturePath", str(folder / "capture.png"), "-CaptureAfterFrames", str(warmup)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    start = time.monotonic()
    with (folder / "process.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = child.wait(timeout=300)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise RuntimeError("Capture timeout: " + str(folder))
    text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
    if code or "[ERROR]" in text or "[CORRUPTION]" in text or "effectivePath=1" not in text:
        raise RuntimeError(f"Capture failed: {folder}, exit={code} (0x{code & 0xffffffff:08x})")
    record = dict(command=command, processWallSeconds=time.monotonic() - start,
                  warningCount=text.count("[WARNING]"), shaderSha256={p: sha(exe.parent / p) for p in (CSO, PSO)},
                  inputSha256={p: sha(folder / p) for p in ("scene.json", "render-preset.json")})
    if not mask:
        with Image.open(folder / "capture.png") as image:
            if image.size != (size, size):
                raise RuntimeError("Wrong PNG dimensions")
        record["imageSha256"] = sha(folder / "capture.png")
        return record, None
    exrs = list(folder.glob("*.exr"))
    if len(exrs) != 1:
        raise RuntimeError("Expected one mask EXR")
    raw = folder / "mask.rgba32f"
    decoded = subprocess.run([str(decoder), str(exrs[0]), "export-rgba32f", str(raw)],
                             capture_output=True, text=True, check=True, timeout=30)
    if decoded.stdout.strip() != f"{size} {size}":
        raise RuntimeError("Wrong EXR dimensions")
    pixels = np.fromfile(raw, dtype="<f4").reshape(size, size, 4)
    if not np.isfinite(pixels).all() or np.max(np.abs(pixels[:, :, :3] - pixels[:, :, :1])) > 1e-5:
        raise RuntimeError("Invalid or nongrayscale visibility")
    visibility = pixels[:, :, 0]
    if visibility.min() < 0 or visibility.max() > 1:
        raise RuntimeError("Visibility outside [0,1]")
    np.save(folder / "mask.npy", visibility)
    Image.fromarray(np.uint8(np.clip(visibility, 0, 1) * 255 + .5)).save(folder / "mask.png")
    record["imageSha256"] = sha(exrs[0])
    return record, visibility


def metrics(image, reference, size):
    rois = dict(back_wall=(190, 170, 530, 350), floor=(140, 615, 580, 660),
                diffuse_contact=(390, 505, 540, 645))
    result = {}
    for label, rect in rois.items():
        x0, y0, x1, y1 = [round(value * size / 720) for value in rect]
        error = image[y0:y1, x0:x1] - reference[y0:y1, x0:x1]
        result[label] = dict(mae=float(np.abs(error).mean()), rmse=float(np.sqrt(np.mean(error**2))),
            bias=float(error.mean()), p95AbsoluteError=float(np.percentile(np.abs(error), 95)),
            meanVisibility=float(image[y0:y1, x0:x1].mean()),
            residualGradientRms=float(np.sqrt((np.mean(np.diff(error, axis=0)**2) +
                                                np.mean(np.diff(error, axis=1)**2)) / 2)))
    return result


def compose(output, names, size, suffix=""):
    font = ImageFont.truetype("C:/Windows/Fonts/meiryo.ttc", 20)
    labels = {"fixed64": "現状：固定64本", "scrambled64-filter": "試作：分散64本＋フィルタ",
              "reference4096": "参照：固定4096本"}
    sheet = Image.new("RGB", (size * len(names), size + 86), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(names):
        with Image.open(output / name / "lit/capture.png") as image:
            sheet.paste(image.convert("RGB"), (index * size, 48))
        draw.text((index * size + 10, 10), labels.get(name, name), fill="white", font=font)
        draw.text((index * size + 10, size + 53), "同一カメラ・露出1・ACES", fill="white", font=font)
    sheet.save(output / ("comparison" + suffix + ".png"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Existing Cornell scene/preset folder")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, default=360)
    parser.add_argument("--decoder", type=Path, required=True)
    parser.add_argument("--variants", nargs="+", help="Subset, including fixed64, scrambled64-filter and reference4096")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = output / "runtime"
    runtime.mkdir()
    original = ROOT / "bin/x64/Debug"
    for path in original.iterdir():
        if path.is_file() and path.suffix.lower() in (".exe", ".dll", ".cso"):
            shutil.copy2(path, runtime / path.name)
    shutil.copytree(original / "D3D12", runtime / "D3D12")
    # Read-only asset hard links avoid duplicating large managed models.
    shutil.copytree(original / "Assets", runtime / "Assets", copy_function=os.link)
    dxc = ROOT / "packages/Microsoft.Direct3D.DXC.1.8.2505.32/build/native/bin/x64/dxc.exe"
    exe = runtime / "RtPbrSurvey.exe"
    scene = json.loads((args.source / "scene.json").read_text(encoding="utf-8"))
    for asset in scene["assets"]:
        asset["path"] = str((args.source / asset["path"]).resolve())
    preset = json.loads((args.source / "render-preset.json").read_text(encoding="utf-8"))
    preset["renderViewMode"] = 0
    preset["diffuseEnvironmentVisibility"].update(enabled=True, strength=1)
    source = (SHADERS / "shaders_LightPass.hlsl").read_text(encoding="utf-8")
    replacement = "float visibility = saturate(g_diffuseEnvironmentVisibility.Load(int3(input.position.xy, 0)));"
    if replacement not in source:
        replacement = "float visibility = saturate(SampleDiffuseEnvironmentVisibility(int2(input.position.xy), normal, worldPos));"
    if source.count(replacement) != 1:
        raise RuntimeError("LightPass study insertion point changed")
    source = source.replace("float4 PSMain(", '#include "visibility_filter_study.hlsli"\n\nfloat4 PSMain(', 1)
    source = source.replace(replacement,
        "float visibility = saturate(StudyFilteredVisibility(int2(input.position.xy), normal, worldPos));\n"
        "#if STUDY_MASK_VIEW\n        return float4(visibility.xxx, 1);\n#endif")
    snapshot = output / "sources"
    snapshot.mkdir()
    for path in (HERE / "visibility_sampling_study.hlsl", HERE / "visibility_filter_study.hlsli",
                 SHADERS / "shaders_LightPass.hlsl", Path(__file__)):
        shutil.copy2(path, snapshot / path.name)
    study_light = output / "study_light.hlsl"
    study_light.write_text(source, encoding="utf-8", newline="\r\n")
    variants = [("fixed64", 64, 0, 0, 0), ("fixed64-filter", 64, 0, 4, 0),
                ("scrambled64", 64, 1, 0, 0), ("scrambled64-filter", 64, 1, 4, 0),
                ("scrambled64-filter-seed1", 64, 1, 4, 1),
                ("reference1024", 1024, 0, 0, 0), ("reference4096", 4096, 0, 0, 0)]
    if args.variants:
        required = {"fixed64", "scrambled64-filter", "reference4096"}
        if not required.issubset(args.variants) or set(args.variants) - {v[0] for v in variants}:
            parser.error("Unknown variant or missing required comparison variants")
        variants = [variant for variant in variants if variant[0] in args.variants]
    report = dict(status="running", scope="Static Cornell feasibility; not production quality or performance certification",
                  executableSha256=sha(exe), size=args.size, records=[],
                  sourceSha256={p.name: sha(p) for p in (HERE / "visibility_sampling_study.hlsl",
                      HERE / "visibility_filter_study.hlsli", SHADERS / "shaders_LightPass.hlsl")})
    masks = {}
    write_json(output / "report.json", report)
    try:
        for name, samples, scramble, radius, seed in variants:
            compile_shader(dxc, HERE / "visibility_sampling_study.hlsl", runtime / CSO, "cs_6_5", "CSMain",
                           dict(STUDY_SAMPLES=samples, STUDY_SCRAMBLE=scramble, STUDY_SEED=seed))
            for kind in ("mask", "lit"):
                folder = output / name / kind
                folder.mkdir(parents=True)
                local = copy.deepcopy(preset)
                if kind == "mask":
                    local["toneMap"] = dict(operatorIndex=0, exposure=1)
                local_scene = copy.deepcopy(scene)
                for asset in local_scene["assets"]:
                    asset["path"] = Path(os.path.relpath(asset["path"], folder)).as_posix()
                write_json(folder / "scene.json", local_scene)
                write_json(folder / "render-preset.json", local)
                compile_shader(dxc, study_light, runtime / PSO, "ps_6_0", "PSMain",
                               dict(STUDY_RADIUS=radius, STUDY_MASK_VIEW=int(kind == "mask")))
                record, mask = capture(exe, folder, args.decoder.resolve(), args.size, kind == "mask")
                report["records"].append(dict(name=name, kind=kind, samples=samples, scramble=scramble,
                                              filterRadius=radius, seed=seed, **record))
                if mask is not None:
                    masks[name] = mask
                write_json(output / "report.json", report)
                print(name + "/" + kind, flush=True)
        report["metrics"] = {name: metrics(mask, masks["reference4096"], args.size) for name, mask in masks.items()}
        if "scrambled64-filter-seed1" in masks:
            report["seedDifference"] = metrics(masks["scrambled64-filter"], masks["scrambled64-filter-seed1"], args.size)
        compose(output, ["fixed64", "scrambled64-filter", "reference4096"], args.size)
        if "scrambled64" in masks and "fixed64-filter" in masks:
            compose(output, ["scrambled64", "fixed64-filter", "scrambled64-filter"], args.size, "-controls")
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""), flush=True)
    return report["status"] != "complete"


if __name__ == "__main__":
    raise SystemExit(main())
