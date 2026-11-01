"""Validate diffuse-only visibility multiplication in linear scene-color EXR."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from PIL import Image

from inspect_hybrid_ibl_visibility import prepare, ROOT
from validate_part1 import sha, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--lighting", nargs="+", choices=("diffuse", "specular", "direct"),
                        default=["diffuse", "specular", "direct"])
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prepare(output, ("cornell",))
    cases = [("open-off", "open", "diffuse", False, 1, None, None),
        ("open-on", "open", "diffuse", True, 1, "composite-open", "open-off"),
        ("open-half", "open", "diffuse", True, .5, "composite-half", "open-off"),
        ("open-zero", "open", "diffuse", True, 0, "unchanged", "open-off"),
        ("closed-off", "closed", "diffuse", False, 1, None, None),
        ("closed-on", "closed", "diffuse", True, 1, "composite-closed", "closed-off"),
        ("specular-off", "open", "specular", False, 1, None, None),
        ("specular-on", "open", "specular", True, 1, "unchanged", "specular-off"),
        ("direct-off", "open", "direct", False, 1, None, None),
        ("direct-on", "open", "direct", True, 1, "unchanged", "direct-off")]
    cases = [case for case in cases if case[2] in args.lighting]
    exe = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
    report = dict(status="running", records=[], executableSha256=sha(exe),
        shaderSha256={name: sha(exe.parent / name) for name in
            ("shaders_LightPass_PSMain.cso", "shaders_DiffuseEnvironmentVisibility_CSMain.cso")})
    write_json(output / "report.json", report)
    images = {}
    try:
        for label, enclosure, lighting, enabled, strength, check, reference in cases:
            source = output / "cornell" / (enclosure + "-diffuse")
            folder = output / label
            folder.mkdir()
            scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
            preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
            for asset in scene["assets"]:
                asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
            preset["renderViewMode"] = 0
            preset["toneMap"] = dict(operatorIndex=0, exposure=1)
            preset["diffuseEnvironmentVisibility"] = dict(enabled=enabled, strength=strength,
                sampleRotationEnabled=False, spatialFilterEnabled=False)
            preset["lighting"].update(diffuseIblEnabled=lighting == "diffuse",
                specularIblEnabled=lighting == "specular", directLightEnabled=lighting == "direct")
            if lighting == "direct":
                preset["lighting"].update(primaryShadowLightId=1, lights=[dict(id=1, name="Control",
                    type="directional", enabled=True, color=[1, 1, 1], intensity=1, direction=[0, 0, 1],
                    position=[0, 0, 0], range=20, innerCone=15, outerCone=30)])
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", preset)
            log = folder / "debug.log"
            command = [str(exe), "-SceneFile", str(folder / "scene.json"), "-WindowSize", "640", "360",
                "-CaptureSessionOutputDir", str(folder), "-CaptureSessionBaseName", "color",
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
                    raise RuntimeError("Capture timeout: " + label)
            text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
            captures = list(folder.glob("*.exr"))
            if code or len(captures) != 1 or "[ERROR]" in text or "[CORRUPTION]" in text:
                raise RuntimeError(f"Capture failure: {label}, exit={code} (0x{code & 0xffffffff:08x}), EXR count={len(captures)}")
            images[label] = captures[0]
            record = dict(case=label, command=command, imageSha256=sha(captures[0]),
                inputSha256={p.name: sha(p) for p in (folder / "scene.json", folder / "render-preset.json")},
                warningCount=text.count("[WARNING]"))
            if check:
                result = subprocess.run([str(args.validator.resolve()), str(captures[0]), check,
                    str(images[reference])], capture_output=True, text=True, timeout=20)
                record.update(validation=result.stdout + result.stderr, passed=result.returncode == 0)
                preview = Path(str(captures[0]) + ".ppm")
                if preview.exists():
                    with Image.open(preview) as image:
                        image.save(folder / "preview.png")
                if result.returncode:
                    report["records"].append(record)
                    raise RuntimeError("Composite mismatch: " + label)
            report["records"].append(record)
            write_json(output / "report.json", report)
            print(label + ": " + record.get("validation", "captured").strip(), flush=True)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""))
    return report["status"] != "passed"


if __name__ == "__main__":
    raise SystemExit(main())
