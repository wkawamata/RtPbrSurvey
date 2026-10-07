"""Decode real GPU captures, including H.264 MP4; keep artifacts under bin."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

import validate_standalone_close as capture

ROOT = capture.ROOT
INSPECTOR = ROOT / "build/scene-document-tests/Debug/RtPbrSurvey.Mp4EncoderTests.exe"


def validate_outputs(directory, case, accepted):
    from PIL import Image
    files = sorted((directory / "outputs" / "nested").glob("*"))
    width, height = case.get("roi", (0, 0, 1920, 1080))[2:]
    expected_size = (width, height)
    result = {"files": [str(path.relative_to(directory)) for path in files]}
    if case["format"] in ("png", "exr"):
        assert len(files) == len(accepted), "Accepted/saved frame counts differ"
        for path in files:
            if case["format"] == "png":
                assert capture.png_size(path) == expected_size
            else:
                assert capture.exr_readable(path)
    elif case["format"] == "gif":
        assert len(files) == 1
        with Image.open(files[0]) as image:
            assert image.n_frames == len(accepted)
            assert image.info.get("loop") == 2, "Requested GIF repeat count was lost"
            for index in range(image.n_frames):
                image.seek(index)
                image.convert("RGBA").load()
                assert image.size == expected_size
    else:
        assert len(files) == 1 and files[0].suffix == ".mp4"
        encoded_size = (width + width % 2, height + height % 2)
        if case.get("duration"):
            command = [str(INSPECTOR), "--duration", str(files[0]),
                       *map(str, encoded_size), str(case["fps"]), str(case["duration"])]
        else:
            command = [str(INSPECTOR), str(files[0]), *map(str, encoded_size),
                       str(len(accepted)), str(case["fps"])]
        decoded = subprocess.run(command, capture_output=True, text=True)
        (directory / "decode.log").write_text(decoded.stdout + decoded.stderr, encoding="utf-8")
        assert decoded.returncode == 0, decoded.stdout + decoded.stderr
        decoded_count = re.search(r"(\d+) frames", decoded.stdout)
        assert decoded_count and int(decoded_count.group(1)) == len(accepted), "Decoded/accepted video frame counts differ"
        result["decoder"] = decoded.stdout.strip()
        result["decodeCommand"] = command
    result["sha256"] = {str(path.relative_to(directory)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in files}
    return result


def run_case(output, case):
    directory = output / case["name"]
    directory.mkdir()
    log = directory / "d3d12.log"
    command = [str(capture.EXECUTABLE), "-SceneFile", str(capture.SCENE),
               "-RenderPreset", str(capture.PRESET), "-CaptureSessionOutputDir", str(directory / "outputs"),
               "-CaptureSessionSubfolder", "nested", "-CaptureSessionBaseName", "movie",
               "-CaptureSessionFormat", case["format"], "-CaptureSessionClock", case.get("clock", "fixed-step"),
               "-CaptureSessionFps", str(case["fps"]), "-CaptureSessionFrames", str(case["frames"]),
               "-CaptureSessionWarmupFrames", "3", "-LogToFile", str(log), "-ExitAfterCapture"]
    if "roi" in case:
        command += ["-CaptureSessionRoi", *map(str, case["roi"])]
    if case["format"] == "mp4":
        command += ["-CaptureSessionMp4BitrateMbps", "8"]
    if case["format"] == "gif":
        command += ["-CaptureSessionGifRepeat", "2"]
    if case.get("duration"):
        command += ["-CaptureSessionDurationSeconds", str(case["duration"])]
    if "renderingPath" in case:
        preset = json.loads(capture.PRESET.read_text(encoding="utf-8"))
        preset["renderingPath"] = case["renderingPath"]
        preset_path = directory / "render-preset.json"
        preset_path.write_text(json.dumps(preset, indent=2), encoding="utf-8")
        command[command.index("-RenderPreset") + 1] = str(preset_path)
    child = capture.start(command, directory)
    try:
        code = child.wait(timeout=150)
        assert code == 0, f"Application exited with {code}"
        accepted = capture.frames(log)
        assert accepted, "No GPU capture was accepted"
        if not case.get("duration"):
            assert len(accepted) == case["frames"], "Frame limit not honored"
        assert [frame[0] for frame in accepted] == list(range(len(accepted)))
        if case.get("clock", "fixed-step") == "fixed-step":
            assert all(abs(b[1] - a[1] - 1 / case["fps"]) < 2e-9
                       for a, b in zip(accepted, accepted[1:])), "Fixed-step clock drift"
        text = capture.read_log(log)
        errors, save_failures = capture.classify_errors(text)
        assert not errors and not save_failures, "Capture or D3D12 error"
        outputs = validate_outputs(directory, case, accepted)
        return {"name": case["name"], "command": command, "exitCode": code,
                "captureFrames": accepted, "d3d12Errors": errors, **outputs}
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cases", help="Comma-separated case names")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / "bin/CapturePort/python-packages"))
    sys.path.insert(0, str(ROOT / "bin/PathTracingValidation/python-packages"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = [
        dict(name="png-roi", format="png", fps=60, frames=3, roi=(100, 100, 64, 64)),
        dict(name="png-full", format="png", fps=30, frames=2),
        dict(name="exr-sequence", format="exr", fps=30, frames=3, roi=(100, 100, 64, 64)),
        dict(name="gif-repeat", format="gif", fps=30, frames=3, roi=(100, 100, 64, 64)),
        dict(name="mp4-odd-roi-60", format="mp4", fps=60, frames=5, roi=(100, 100, 63, 47)),
        dict(name="mp4-roi-30", format="mp4", fps=30, frames=5, roi=(100, 100, 64, 64)),
        dict(name="mp4-full", format="mp4", fps=30, frames=3),
        dict(name="mp4-real-time", format="mp4", fps=60, frames=100, duration=1.5,
             clock="real-time", roi=(100, 100, 64, 64)),
    ]
    for name, rendering_path in (("forward", 0), ("path-tracing", 2)):
        for output_format in ("png", "mp4"):
            cases.append(dict(name=f"{output_format}-{name}", format=output_format, fps=30,
                              frames=3, roi=(100, 100, 64, 64), renderingPath=rendering_path))
    if args.cases:
        requested = set(args.cases.split(","))
        assert requested <= {case["name"] for case in cases}, "Unknown case name"
        cases = [case for case in cases if case["name"] in requested]
    report = {"records": [], "testedCommit": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "executableSha256": hashlib.sha256(capture.EXECUTABLE.read_bytes()).hexdigest(),
        "inspectorSha256": hashlib.sha256(INSPECTOR.read_bytes()).hexdigest(),
        "scriptSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    for case in cases:
        try:
            record = run_case(output, case)
        except Exception as error:
            report.update(failedCase=case["name"], failure=str(error))
            (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            raise
        report["records"].append(record)
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(record), flush=True)
    print(f"All {len(cases)} GPU format cases passed.", flush=True)


if __name__ == "__main__":
    main()
