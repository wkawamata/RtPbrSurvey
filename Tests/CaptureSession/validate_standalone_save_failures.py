"""Verify runtime capture save failures through CLI without GUI input."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

import validate_standalone_close as capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = {"testedCommit": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=capture.ROOT, text=True).strip(),
        "executableSha256": hashlib.sha256(capture.EXECUTABLE.read_bytes()).hexdigest(),
        "records": []}
    for output_format in ("png", "exr", "gif", "mp4"):
        directory = output / output_format
        directory.mkdir()
        blocker = directory / "blocker.txt"
        blocker.write_text("runtime save failure fixture", encoding="utf-8")
        log = directory / "d3d12.log"
        command = [str(capture.EXECUTABLE), "-SceneFile", str(capture.SCENE),
                   "-RenderPreset", str(capture.PRESET), "-CaptureSessionOutputDir",
                   str(blocker / "output"), "-CaptureSessionBaseName", "movie",
                   "-CaptureSessionFormat", output_format, "-CaptureSessionFrames", "3",
                   "-CaptureSessionRoi", "100", "100", "64", "64",
                   "-CaptureSessionClock", "fixed-step", "-CaptureSessionFps", "30",
                   "-LogToFile", str(log), "-ExitAfterCapture"]
        child = capture.start(command, directory)
        try:
            code = child.wait(timeout=150)
            lines = capture.read_log(log).splitlines()
            save_errors = [line for line in lines if "[ERROR] Capture session failed:" in line]
            gpu_errors = [line for line in lines if "[CORRUPTION]" in line or
                          ("[ERROR]" in line and line not in save_errors)]
            record = {"format": output_format, "command": command, "exitCode": code,
                      "d3d12Errors": gpu_errors, "saveErrors": save_errors}
            report["records"].append(record)
            (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            assert code == 1, f"{output_format}: failure exit code was {code}"
            assert save_errors, f"{output_format}: failure was not logged"
            assert not gpu_errors, f"{output_format}: unexpected GPU error"
            assert blocker.is_file() and not (blocker / "output").exists()
            print(json.dumps(record), flush=True)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
    print("All 4 runtime save failure cases passed.", flush=True)


if __name__ == "__main__":
    main()
