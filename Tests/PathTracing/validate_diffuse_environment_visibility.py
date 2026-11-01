"""Capture the isolated mask and validate its linear EXR values with TinyEXR."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess

from PIL import Image

from inspect_hybrid_ibl_visibility import prepare, ROOT
from capture_pt_hybrid_comparison import node
from validate_part1 import sha, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prepare(output, ("cornell",))
    records = []
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", executableSha256=sha(exe), records=records,
        validator=str(args.validator.resolve()), shaderSha256=sha(exe.parent / "shaders_DiffuseEnvironmentVisibility_CSMain.cso"))
    write_json(output / "mask-report.json", report)
    try:
        for mode in ("open", "closed", "unoccluded", "unsupported"):
            source = output / "cornell" / ("closed-diffuse" if mode == "closed" else "open-diffuse")
            folder = output / ("mask-" + mode)
            folder.mkdir()
            scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
            preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
            for asset in scene["assets"]:
                asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
            if mode in ("unoccluded", "unsupported"):
                plane = node("receiver", "primitive", materialId="closure",
                    primitive=dict(kind="plane", width=10, depth=10))
                plane.update(translation=[0, 0, 1.5], rotation=[-2**-.5, 0, 0, 2**-.5])
                scene["nodes"] = [plane]
                if mode == "unsupported":
                    scene["materials"][-1]["transmission"] = dict(mode="thin", factor=.5, tint=[1, 1, 1], ior=1.5)
            preset["renderViewMode"] = 42  # Append-only RenderViewMode::DiffuseEnvironmentVisibility.
            preset["diffuseEnvironmentVisibility"] = dict(sampleRotationEnabled=False, spatialFilterEnabled=False)
            preset["toneMap"] = dict(operatorIndex=0, exposure=1)
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", preset)
            log = folder / "debug.log"
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", "640", "360",
                "-CaptureSessionOutputDir", str(folder), "-CaptureSessionBaseName", "mask",
                "-CaptureSessionFormat", "exr", "-CaptureSessionFrames", "1",
                "-CaptureSessionWarmupFrames", "3", "-ExitAfterCapture", "-LogToFile", str(log)]
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            with (folder / "process.log").open("wb") as stream:
                child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup,
                    stdout=stream, stderr=subprocess.STDOUT)
                try:
                    code = child.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                    raise RuntimeError("Capture timeout: " + mode)
            text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
            images = list(folder.glob("*.exr"))
            if code or len(images) != 1 or "[ERROR]" in text or "[CORRUPTION]" in text:
                raise RuntimeError("Capture failure: " + mode)
            check = subprocess.run([str(args.validator.resolve()), str(images[0]), mode],
                capture_output=True, text=True, timeout=20)
            preview = Path(str(images[0]) + ".ppm")
            if preview.exists():
                with Image.open(preview) as source_image:
                    source_image.save(folder / "mask-preview.png")
            records.append(dict(mode=mode, command=command, imageSha256=sha(images[0]),
                inputSha256={p.name: sha(p) for p in (folder / "scene.json", folder / "render-preset.json")},
                validation=check.stdout + check.stderr, passed=check.returncode == 0,
                warningCount=text.count("[WARNING]")))
            write_json(output / "mask-report.json", report)
            print(mode + ": " + check.stdout.strip(), flush=True)
            if check.returncode:
                raise RuntimeError("Mask validation failure: " + mode)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "mask-report.json", report)
    print(report["status"], report.get("error", ""))
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
