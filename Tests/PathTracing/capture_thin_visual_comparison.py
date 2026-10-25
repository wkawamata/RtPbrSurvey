"""Capture the saved thin-transmission comparison; this is not a quality verdict."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageStat


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scene", type=Path)
    parser.add_argument("--nrd-preview", action="store_true")
    parser.add_argument("--frames", type=int, default=256)
    args = parser.parse_args()
    if not 1 <= args.frames <= 4096:
        parser.error("--frames must be in [1, 4096]")
    root = Path(__file__).resolve().parents[2]
    exe = args.exe.resolve(strict=True)
    scene_path = (args.scene or root / "Assets/Scene/PathTracingValidation/20-thin-transmission-visual-comparison/scene.json").resolve(strict=True)
    scene_dir = scene_path.parent
    scene = json.loads(scene_path.read_text(encoding="utf-8"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    image = output / (scene_dir.name + ".png")
    log = output / "capture.log"
    command = [str(exe), "-SceneFile", str(scene_path),
               "-CapturePath", str(image), "-CaptureAfterFrames", str(args.frames),
               "-LogToFile", str(log), "-ExitAfterCapture"]
    if args.nrd_preview:
        command.append("-NrdPreview")
    report = {"status": "running", "command": command, "frames": args.frames,
              "executableSha256": digest(exe), "runnerSha256": digest(Path(__file__)),
              "inputs": {}, "failures": []}
    inputs = {scene_path, (scene_dir / scene["renderPreset"]).resolve(strict=True)}
    for asset in scene.get("assets", []):
        asset_path = (scene_dir / asset["path"]).resolve(strict=True)
        if asset_path.suffix.lower() != ".gltf":
            parser.error("This runner inventories .gltf assets only")
        inputs.add(asset_path)
        gltf = json.loads(asset_path.read_text(encoding="utf-8"))
        for resource in gltf.get("buffers", []) + gltf.get("images", []):
            uri = resource.get("uri", "")
            if uri and not uri.startswith("data:"):
                inputs.add((asset_path.parent / uri).resolve(strict=True))
    for path in sorted(inputs):
        report["inputs"][str(path.relative_to(root))] = digest(path)
    try:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        child = subprocess.Popen(command, cwd=root, startupinfo=startup,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            code = child.wait(timeout=180)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            raise RuntimeError("Capture timed out")
        text = log.read_text(encoding="utf-8-sig")
        if code or "[ERROR]" in text or "[CORRUPTION]" in text:
            raise RuntimeError("Runtime or D3D12 Debug Layer failure")
        records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines()
                   if line.startswith("[PathTracing] ")]
        if len(records) != 1:
            raise RuntimeError("Expected exactly one PT capture record")
        record = records[0]
        presentation = ("Native fallback: thin transmission is not supported by NRD"
                        if args.nrd_preview else "Native PT")
        if (record.get("scene") != scene["name"]
                or record.get("accumulatedSamples") != args.frames
                or record.get("samplesPerFrame") != 1
                or record.get("maxBounces") != 8
                or record.get("nrdPreviewRequested") is not args.nrd_preview
                or record.get("nrdPresentation") != presentation
                or record.get("nrdPlannedDispatches") != 0
                or record.get("nrdPreparedDispatches") != 0
                or record.get("nrdFailureCount") != 0
                or record.get("d3d12DebugLayerEnabledByApp") is not True):
            raise RuntimeError("PT capture settings or scene did not match the saved fixture")
        with Image.open(image) as picture:
            picture.load()
            if min(picture.size) < 128 or max(ImageStat.Stat(picture.convert("RGB")).stddev) < 1:
                raise RuntimeError("Capture is too small or uniform")
            report["dimensions"] = list(picture.size)
        report.update(status="passed-runtime-smoke-not-subjective-approval",
                      diagnostics=records[0], imageSha256=digest(image), logSha256=digest(log))
    except Exception as error:
        report["status"] = "failed"
        report["failures"].append(str(error))
        raise
    finally:
        payload = (json.dumps(report, ensure_ascii=False, indent=2) + "\n").replace("\n", "\r\n")
        (output / "report.json").write_bytes(payload.encode("utf-8"))
    print(report["status"])


if __name__ == "__main__":
    main()
