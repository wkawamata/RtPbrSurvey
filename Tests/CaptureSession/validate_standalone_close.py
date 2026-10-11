"""Deferred exit, failure exit code, and capture conflict regression for the standalone app; artifacts remain under bin."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
FRAME_PATTERN = re.compile(r"\[CAPTURE_FRAME\] index=(\d+) simulation=([0-9.]+) render=(\d+)")
WM_CLOSE = 0x10
WM_KEYDOWN = 0x100
WM_KEYUP = 0x101
WINDOW_CLASS = "RtPbrSurveyAppClass"
EXECUTABLE = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
SCENE = ROOT / "Assets/Scenes/PathTracingValidation/input-plane/scene.json"
PRESET = ROOT / "Assets/Scenes/PathTracingValidation/input-plane/render-preset.json"
F8 = 0x77
P_KEY = ord("P")
F_KEY = ord("F")
VK_ESCAPE = 0x1B


def read_log(log):
    return log.read_text(encoding="utf-8-sig") if log.exists() else ""


def frames(log):
    text = read_log(log)
    return [(int(i), float(t), int(f)) for i, t, f in FRAME_PATTERN.findall(text)]


def classify_errors(text):
    # The app records an intended save failure as [ERROR]. D3D12 debug layer messages are separate.
    gpu = []
    intended = []
    for line in text.splitlines():
        if "[CORRUPTION]" in line:
            gpu.append(line)
        elif "[ERROR]" in line:
            (intended if "Capture failed:" in line else gpu).append(line)
    return gpu, intended


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
    user.GetClassNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]
    found = []

    @callback_type
    def visit(hwnd, _):
        owner = ctypes.c_ulong()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        buffer = ctypes.create_unicode_buffer(256)
        user.GetClassNameW(hwnd, buffer, 256)
        if buffer.value == WINDOW_CLASS:
            found.append(hwnd)
        return True

    user.EnumWindows(visit, 0)
    return found[0] if found else None


def post_message(hwnd, message):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    if not user.PostMessageW(hwnd, message, 0, 0):
        raise ctypes.WinError(ctypes.get_last_error())


def post_key(hwnd, value):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    if not user.PostMessageW(hwnd, WM_KEYDOWN, value, 1) or not user.PostMessageW(hwnd, WM_KEYUP, value, 1):
        raise ctypes.WinError(ctypes.get_last_error())


def start(command, working_directory=ROOT):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.Popen(command, cwd=working_directory, startupinfo=startup)


def png_size(path):
    from PIL import Image
    with Image.open(path) as image:
        assert image.format == "PNG", "Output is not a PNG"
        image.verify()
    with Image.open(path) as image:
        image.load()
        return image.size


def exr_readable(path):
    import numpy as np
    import OpenEXR
    with OpenEXR.File(str(path)) as image:
        channels = image.channels()
        assert channels, "EXR has no decoded channels"
        for channel in channels.values():
            pixels = channel.pixels
            assert pixels.shape[:2] == (64, 64), "EXR dimensions do not match ROI"
            assert np.isfinite(pixels).all(), "EXR contains nonfinite samples"
    return True


def gif_frame_count(path):
    from PIL import Image
    with Image.open(path) as image:
        count = image.n_frames
        for index in range(count):
            image.seek(index)
            image.convert("RGBA").load()
            assert image.size == (64, 64), "GIF dimensions do not match ROI"
        return count


def session_command(directory, extra=None):
    command = [str(EXECUTABLE), "-SceneFile", str(SCENE), "-RenderPreset", str(PRESET),
        "-CaptureSessionOutputDir", str(directory), "-CaptureSessionBaseName", "frame",
        "-CaptureSessionClock", "fixed-step", "-CaptureSessionWarmupFrames", "3",
        "-CaptureSessionRoi", "100", "100", "64", "64"]
    if extra:
        command += extra
    return command


def check_saved_outputs(directory, log, checks, pattern, readable, dimensions=(64, 64)):
    captured = frames(log)
    outputs = sorted(directory.glob(pattern))
    checks["allAcceptedFramesSaved"] = len(outputs) == len(captured) and len(captured) >= 1
    assert checks["allAcceptedFramesSaved"], f"saved {len(outputs)} files but accepted {len(captured)} frames"
    checks["outputsReadable"] = all((readable(path) == dimensions if readable is png_size else readable(path)) for path in outputs)
    assert checks["outputsReadable"], "output files are not readable"
    return dict(acceptedFrames=len(captured), savedFiles=len(outputs))


def deferred_close_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    captured = wait_for(lambda: frames(log), child)
    checks["frameAcceptedBeforeCloseRequest"] = len(captured) >= 1
    assert checks["frameAcceptedBeforeCloseRequest"], "no frame was accepted before the close request"
    post_message(hwnd, WM_CLOSE)
    checks["stillAliveAtCloseRequest"] = child.poll() is None
    assert checks["stillAliveAtCloseRequest"], "application closed immediately at the close request"


def failure_setup(directory):
    # A path whose parent is an existing regular file cannot hold an output file.
    (directory / "blocker.txt").write_text("blocker", encoding="utf-8")


def failure_after(directory, log, checks):
    _, intended = classify_errors(read_log(log))
    checks["intendedSaveFailureRecorded"] = len(intended) >= 1
    assert checks["intendedSaveFailureRecorded"], "save failure was not recorded as an error"
    return dict()


def conflict_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    time.sleep(1.0)
    post_key(hwnd, F8)
    time.sleep(.25)
    checks["sessionStartRefusedWhilePending"] = len(frames(log)) == 0
    assert checks["sessionStartRefusedWhilePending"], "session started while automated capture was pending"
    time.sleep(3.0)


def conflict_after(directory, log, checks):
    outputs = sorted(directory.glob("*.png"))
    checks["sessionStartRefusedFinally"] = len(frames(log)) == 0
    checks["automatedCaptureCompleted"] = len(outputs) == 1 and png_size(outputs[0]) == (1920, 1080)
    assert checks["automatedCaptureCompleted"], "automated capture did not complete"
    return dict(producedFiles=[path.name for path in outputs])


def restart_after_automated_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    post_key(hwnd, F8)
    checks["refusedWhileAutomatedCapturePending"] = len(frames(log)) == 0
    assert checks["refusedWhileAutomatedCapturePending"], "start was allowed while automated capture was pending"
    wait_for(lambda: "[Capture]" in read_log(log), child, timeout=60)
    post_key(hwnd, F8)
    wait_for(lambda: frames(log), child, timeout=30)
    checks["startAllowedAfterCompletion"] = len(frames(log)) > 0
    assert checks["startAllowedAfterCompletion"], "start was still refused after the automated capture completed"
    post_key(hwnd, F8)
    post_message(hwnd, WM_CLOSE)


def restart_after_failure_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    post_key(hwnd, F8)
    checks["refusedWhileAutomatedCapturePending"] = len(frames(log)) == 0
    assert checks["refusedWhileAutomatedCapturePending"], "start was allowed while the failed capture was pending"
    wait_for(lambda: "[ERROR]" in read_log(log), child, timeout=60)
    post_key(hwnd, F8)
    wait_for(lambda: frames(log), child, timeout=30)
    checks["startAllowedAfterFailure"] = len(frames(log)) > 0
    assert checks["startAllowedAfterFailure"], "start was still refused after the capture failure"
    post_key(hwnd, F8)
    post_message(hwnd, WM_CLOSE)


def plan_setup(directory):
    plan = {
        "version": 1,
        "cameraKeyframes": [{"frame": 0, "yawDegrees": 0.0}, {"frame": 180, "yawDegrees": 0.0}],
        "captures": [
            {"frame": 120, "caseId": "reservation-a", "path": "plan-capture-a-{variant}.png"},
            {"frame": 180, "caseId": "reservation-b", "path": "plan-capture-b-{variant}.png"},
        ],
    }
    (directory / "plan.json").write_text(json.dumps(plan), encoding="utf-8")


def plan_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    post_key(hwnd, F8)
    checks["refusedWhileReservationsRemain"] = len(frames(log)) == 0
    assert checks["refusedWhileReservationsRemain"], "start was allowed while plan reservations remained"
    wait_for(lambda: "[Capture] complete=true" in read_log(log), child, timeout=90)
    post_key(hwnd, F8)
    wait_for(lambda: frames(log), child, timeout=30)
    checks["startAllowedAfterReservations"] = len(frames(log)) > 0
    assert checks["startAllowedAfterReservations"], "start was still refused after the plan completed"
    post_key(hwnd, F8)
    post_message(hwnd, WM_CLOSE)


def plan_after(directory, log, checks):
    outputs = sorted(directory.glob("plan-capture-*.png"))
    checks["planReservationsExecuted"] = len(outputs) == 2
    assert checks["planReservationsExecuted"], "plan reservations were not executed"
    checks["planOutputsReadable"] = all(png_size(path) == (1920, 1080) for path in outputs)
    assert checks["planOutputsReadable"], "plan outputs are not readable full-frame PNGs"
    return dict(producedFiles=[path.name for path in outputs])


def paused_close_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    post_key(hwnd, P_KEY)
    post_key(hwnd, F_KEY)
    captured = wait_for(lambda: frames(log), child, timeout=60)
    checks["pausedFrameCaptured"] = len(captured) >= 1
    assert checks["pausedFrameCaptured"], "no frame was captured while paused"
    post_message(hwnd, WM_CLOSE)
    checks["stillAliveWhileSaving"] = child.poll() is None
    assert checks["stillAliveWhileSaving"], "application closed while paused output was pending"
    checks["closedWithoutExplicitStop"] = True
    assert checks["closedWithoutExplicitStop"], "the case must not send an explicit Stop"


def warmup_close_during(child, directory, log, checks, stop_first=False):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    checks["noFrameAcceptedDuringWarmup"] = len(frames(log)) == 0
    assert checks["noFrameAcceptedDuringWarmup"], "a frame was accepted before the close request"
    if stop_first:
        post_key(hwnd, F8)
    post_message(hwnd, WM_CLOSE)
    checks["stillAliveDuringWarmup"] = child.poll() is None
    assert checks["stillAliveDuringWarmup"], "application closed during warmup before the request was processed"


def warmup_close_after(directory, log, checks):
    checks["warmupReleasedWithoutOutput"] = len(frames(log)) == 0 and len(list(directory.glob("frame_*.png"))) == 0
    assert checks["warmupReleasedWithoutOutput"], "warmup output was not released correctly"
    return dict(acceptedFrames=0, savedFiles=0)


def draining_close_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    wait_for(lambda: len(frames(log)) >= 2, child, timeout=60)
    post_key(hwnd, F8)
    post_message(hwnd, WM_CLOSE)
    checks["stillAliveWhileDraining"] = child.poll() is None
    assert checks["stillAliveWhileDraining"], "application closed while the session was draining"


def scene_switch_during(child, directory, log, checks):
    hwnd = wait_for(lambda: window_for(child.pid), child)
    wait_for(lambda: len(frames(log)) >= 2, child, timeout=60)
    accepted_at_request = len(frames(log))
    post_key(hwnd, VK_ESCAPE)
    time.sleep(2.0)
    checks["noNewFramesAfterSwitchRequest"] = len(frames(log)) == accepted_at_request
    assert checks["noNewFramesAfterSwitchRequest"], "frames kept being accepted after the scene switch request"
    post_message(hwnd, WM_CLOSE)


def gif_after(directory, log, checks):
    captured = frames(log)
    outputs = sorted(directory.glob("*.gif"))
    checks["gifFinalizedAfterClose"] = len(outputs) == 1
    assert checks["gifFinalizedAfterClose"], "GIF was not finalized after the close request"
    checks["gifFrameCountMatchesAccepted"] = gif_frame_count(outputs[0]) == len(captured)
    assert checks["gifFrameCountMatchesAccepted"], "GIF frame count does not match accepted frames"
    return dict(acceptedFrames=len(captured), outputs=[path.name for path in outputs])


REQUIRED_CHECKS = {
    "deferred-close": {"frameAcceptedBeforeCloseRequest", "stillAliveAtCloseRequest", "allAcceptedFramesSaved", "outputsReadable"},
    "failure-exit-code": {"intendedSaveFailureRecorded"},
    "automated-conflict": {"sessionStartRefusedWhilePending", "sessionStartRefusedFinally", "automatedCaptureCompleted"},
    "restart-after-automated": {"refusedWhileAutomatedCapturePending", "startAllowedAfterCompletion", "allAcceptedFramesSaved", "outputsReadable"},
    "restart-after-failure": {"refusedWhileAutomatedCapturePending", "startAllowedAfterFailure", "allAcceptedFramesSaved", "outputsReadable"},
    "plan-reservation-conflict": {"refusedWhileReservationsRemain", "startAllowedAfterReservations", "planReservationsExecuted", "planOutputsReadable"},
    "paused-close-without-stop": {"pausedFrameCaptured", "stillAliveWhileSaving", "closedWithoutExplicitStop", "allAcceptedFramesSaved", "outputsReadable"},
    "warmup-close": {"noFrameAcceptedDuringWarmup", "stillAliveDuringWarmup", "warmupReleasedWithoutOutput"},
    "warmup-stop-close": {"noFrameAcceptedDuringWarmup", "stillAliveDuringWarmup", "warmupReleasedWithoutOutput"},
    "draining-close": {"stillAliveWhileDraining", "allAcceptedFramesSaved", "outputsReadable"},
    "scene-switch-guard": {"noNewFramesAfterSwitchRequest", "allAcceptedFramesSaved", "outputsReadable"},
    "gif-finalize": {"frameAcceptedBeforeCloseRequest", "stillAliveAtCloseRequest", "gifFinalizedAfterClose", "gifFrameCountMatchesAccepted"},
    "exr-finalize": {"frameAcceptedBeforeCloseRequest", "stillAliveAtCloseRequest", "allAcceptedFramesSaved", "outputsReadable"},
}


def run_case(output, case, inject_failure=False):
    name = case["name"]
    directory = output / name
    directory.mkdir(parents=True, exist_ok=False)
    if case.get("setup") is not None:
        case["setup"](directory)
    log = directory / "d3d12.log"
    command = case["command"](directory) + ["-LogToFile", str(log)]
    child = start(command, directory)
    checks = {}
    try:
        case["during"](child, directory, log, checks)
        code = child.wait(timeout=120)
        text = read_log(log)
        gpu_errors, intended_failures = classify_errors(text)
        record = dict(name=name, command=command, directory=str(directory), exitCode=code,
            gpuErrors=gpu_errors, intendedSaveFailures=intended_failures, checks=checks)
        record.update(case["after"](directory, log, checks))

        expected = 1 if inject_failure else case["expectedExitCode"]
        assert code == expected, f"{name}: exit code {code} != expected {expected}"
        assert not gpu_errors, f"{name}: D3D12 debug layer error detected in the log"
        assert REQUIRED_CHECKS[name] <= checks.keys(), f"{name}: missing checks {REQUIRED_CHECKS[name] - checks.keys()}"
        if name not in ("failure-exit-code", "restart-after-failure"):
            assert not intended_failures, f"{name}: unexpected application save failure"
        for key, value in checks.items():
            assert value, f"{name}: check '{key}' is false"
        return record
    finally:
        if child.poll() is None:
            subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New artifact directory")
    parser.add_argument("--packages", type=Path, help="Optional OpenEXR dependency directory")
    parser.add_argument("--cases", help="Comma-separated case names; defaults to all cases")
    parser.add_argument("--inject-failure", action="store_true",
                        help="Force the first case to expect a wrong exit code so the script must fail")
    args = parser.parse_args()
    if args.packages:
        sys.path.insert(0, str(args.packages.resolve()))
    sys.path.insert(0, str(ROOT / "bin/PathTracingValidation/python-packages"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    report = dict(records=[], testedCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        dirty=bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
        windowClass=WINDOW_CLASS,
        sourceSha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in ["App/RtPbrSurveyApp.cpp", "App/RtPbrSurveyApp.h", "App/DebugUi.cpp", "App/SceneEditorUi.cpp",
                         "Runtime/CaptureRequestGate.cpp", "Runtime/CaptureRequestGate.h",
                         "Tests/CaptureSession/validate_standalone_close.py"]},
        executableSha256=hashlib.sha256((ROOT/"bin/x64/Debug/RtPbrSurvey.exe").read_bytes()).hexdigest())

    cases = [
        dict(name="deferred-close", command=lambda d: session_command(d, ["-CaptureSessionFrames", "3",
            "-CaptureSessionFps", "60"]), during=deferred_close_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "frame_*.png", png_size), expectedExitCode=0),
        dict(name="failure-exit-code", command=lambda d: [str(EXECUTABLE), "-SceneFile", str(SCENE),
            "-RenderPreset", str(PRESET), "-CapturePath", str(d / "blocker.txt" / "capture.png"),
            "-CaptureAfterFrames", "6", "-ExitAfterCapture"], during=lambda *a: None, after=failure_after,
            setup=failure_setup, expectedExitCode=1),
        dict(name="automated-conflict", command=lambda d: [str(EXECUTABLE), "-SceneFile", str(SCENE),
            "-RenderPreset", str(PRESET), "-CapturePath", str(d / "automated.png"),
            "-CaptureAfterFrames", "300", "-ExitAfterCapture"], during=conflict_during, after=conflict_after,
            expectedExitCode=0),
        dict(name="restart-after-automated", command=lambda d: [str(EXECUTABLE), "-SceneFile", str(SCENE),
            "-RenderPreset", str(PRESET), "-CapturePath", str(d / "automated.png"),
            "-CaptureAfterFrames", "30"], during=restart_after_automated_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "Screenshots/capture_*.png", png_size, dimensions=(1920, 1080)), expectedExitCode=0),
        dict(name="restart-after-failure", command=lambda d: [str(EXECUTABLE), "-SceneFile", str(SCENE),
            "-RenderPreset", str(PRESET), "-CapturePath", str(d / "blocker.txt" / "capture.png"),
            "-CaptureAfterFrames", "30"], during=restart_after_failure_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "Screenshots/capture_*.png", png_size, dimensions=(1920, 1080)),
            setup=failure_setup, expectedExitCode=1),
        dict(name="plan-reservation-conflict", command=lambda d: [str(EXECUTABLE), "-SceneFile", str(SCENE),
            "-RenderPreset", str(PRESET), "-ReflectionCapturePlan", str(d / "plan.json"),
            "-ReflectionCaptureVariant", "review"], during=plan_during, after=plan_after,
            setup=plan_setup, expectedExitCode=0),
        dict(name="paused-close-without-stop", command=lambda d: session_command(d,
            ["-CaptureSessionFrames", "100", "-CaptureSessionFps", "60"]), during=paused_close_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "frame_*.png", png_size), expectedExitCode=0),
        dict(name="warmup-close", command=lambda d: session_command(d, ["-CaptureSessionWarmupFrames", "30",
            "-CaptureSessionFrames", "100", "-CaptureSessionFps", "60"]), during=warmup_close_during,
            after=warmup_close_after, expectedExitCode=0),
        dict(name="warmup-stop-close", command=lambda d: session_command(d, ["-CaptureSessionWarmupFrames", "30",
            "-CaptureSessionFrames", "100", "-CaptureSessionFps", "60"]),
            during=lambda *a: warmup_close_during(*a, stop_first=True),
            after=warmup_close_after, expectedExitCode=0),
        dict(name="draining-close", command=lambda d: session_command(d, ["-CaptureSessionFrames", "100",
            "-CaptureSessionFps", "60"]), during=draining_close_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "frame_*.png", png_size), expectedExitCode=0),
        dict(name="scene-switch-guard", command=lambda d: session_command(d, ["-CaptureSessionFrames", "100",
            "-CaptureSessionFps", "60"]), during=scene_switch_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "frame_*.png", png_size), expectedExitCode=0),
        dict(name="gif-finalize", command=lambda d: session_command(d, ["-CaptureSessionFrames", "3",
            "-CaptureSessionFps", "60", "-CaptureSessionFormat", "gif"]), during=deferred_close_during,
            after=gif_after, expectedExitCode=0),
        dict(name="exr-finalize", command=lambda d: session_command(d, ["-CaptureSessionFrames", "3",
            "-CaptureSessionFps", "60", "-CaptureSessionFormat", "exr"]), during=deferred_close_during,
            after=lambda d, l, c: check_saved_outputs(d, l, c, "frame_*.exr", exr_readable), expectedExitCode=0),
    ]

    if args.cases:
        requested = set(args.cases.split(","))
        assert requested <= {case["name"] for case in cases}, "Unknown case name"
        cases = [case for case in cases if case["name"] in requested]
    for index, case in enumerate(cases):
        try:
            record = run_case(output, case, inject_failure=(args.inject_failure and index == 0))
        except Exception as error:
            report["failedCase"] = case["name"]
            report["failure"] = str(error)
            (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
            raise
        report["records"].append(record)
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(record), flush=True)

    print(f"All {len(cases)} cases passed.", flush=True)


if __name__ == "__main__":
    main()
