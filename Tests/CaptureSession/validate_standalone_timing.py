"""GPU timing regression for the standalone app; artifacts remain under bin."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
PATTERN = re.compile(r"\[CAPTURE_FRAME\] index=(\d+) simulation=([0-9.]+) render=(\d+)")


def frames(log):
    text = log.read_text(encoding="utf-8-sig") if log.exists() else ""
    return [(int(i), float(t), int(f)) for i, t, f in PATTERN.findall(text)]


def wait_for(check, child, timeout=90):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        if child.poll() is not None:
            raise RuntimeError("Application exited before the expected event")
        time.sleep(.02)
    raise TimeoutError("Expected capture event was not observed")


def window_for(pid):
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_ssize_t)
    user.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    user.EnumWindows.argtypes = [callback_type, ctypes.c_ssize_t]
    found = []

    @callback_type
    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            found.append(hwnd)
        return True

    user.EnumWindows(visit, 0)
    return found[0] if found else None


def key(hwnd, value):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    if not user.PostMessageW(hwnd, 0x100, value, 1) or not user.PostMessageW(hwnd, 0x101, value, 1):
        raise ctypes.WinError(ctypes.get_last_error())


def run(output, fps, clock, pause=False):
    name = f"{clock}-{fps}" + ("-pause-step" if pause else "")
    directory = output / name
    directory.mkdir(parents=True)
    log = directory / "d3d12.log"
    command = [str(ROOT / "bin/x64/Debug/RtPbrSurvey.exe"),
        "-SceneFile", str(ROOT / "Assets/Scenes/PathTracingValidation/input-plane/scene.json"),
        "-RenderPreset", str(ROOT / "Assets/Scenes/PathTracingValidation/input-plane/render-preset.json"),
        "-CaptureSessionOutputDir", str(directory), "-CaptureSessionBaseName", "frame",
        "-CaptureSessionFrames", "100" if pause else "3", "-CaptureSessionFps", str(fps),
        "-CaptureSessionClock", clock, "-CaptureSessionWarmupFrames", "3",
        "-CaptureSessionRoi", "100", "100", "64", "64", "-LogToFile", str(log), "-ExitAfterCapture"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
    checks = {}
    try:
        if pause:
            hwnd = wait_for(lambda: window_for(child.pid), child)
            wait_for(lambda: frames(log), child)
            key(hwnd, ord("P"))
            time.sleep(2)
            before = frames(log)
            time.sleep(1)
            assert frames(log) == before, "Capture clock advanced while paused"
            checks["pausePassed"] = True
            key(hwnd, ord("F"))
            stepped = wait_for(lambda: frames(log) if len(frames(log)) > len(before) else None, child)
            time.sleep(2)
            assert len(frames(log)) == len(before) + 1, "Single step did not advance exactly once"
            assert abs(stepped[-1][1] - before[-1][1] - 1/fps) < 2e-9
            checks["singleStepPassed"] = True
            key(hwnd, 0x77)  # F8 stop remains responsive while P-paused.
        code = child.wait(timeout=120)
        assert code == 0, f"Exit code {code}"
        captured = frames(log)
        assert len(captured) >= 2
        assert [f[0] for f in captured] == list(range(len(captured)))
        intervals = [b[1] - a[1] for a, b in zip(captured, captured[1:])]
        if clock == "fixed-step":
            assert all(abs(dt - 1/fps) < 2e-9 for dt in intervals)
            assert any(b[2] - a[2] > 1 for a, b in zip(captured, captured[1:])), "Readback backpressure was not exercised"
        else:
            assert all(f[1] == 0 for f in captured), "Real-time capture used the simulation clock"
        outputs = sorted(directory.glob("frame_*.png"))
        assert len(outputs) == len(captured)
        for path in outputs:
            data = path.read_bytes()
            assert data[:8] == b"\x89PNG\r\n\x1a\n"
            assert struct.unpack(">II", data[16:24]) == (64, 64)
        text = log.read_text(encoding="utf-8-sig")
        assert "[ERROR]" not in text and "[CORRUPTION]" not in text
        return dict(name=name, command=command, captureFrames=captured, intervals=intervals,
            savedFrames=len(outputs), checks=checks, exitCode=code, d3d12Errors=0)
    finally:
        if child.poll() is None:
            subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New artifact directory")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = dict(records=[], testedCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        sourceSha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in ["App/RtPbrSurveyApp.cpp", "App/RtPbrSurveyApp.h"]},
        executableSha256=hashlib.sha256((ROOT/"bin/x64/Debug/RtPbrSurvey.exe").read_bytes()).hexdigest())
    for fps, clock, pause in [(60, "fixed-step", False), (30, "fixed-step", False),
                              (30, "real-time", False), (60, "fixed-step", True)]:
        record = run(output, fps, clock, pause)
        report["records"].append(record)
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(record), flush=True)


if __name__ == "__main__":
    main()
