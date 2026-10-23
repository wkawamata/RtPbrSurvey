"""Measure interactive camera response and MP4 timing under encoder pressure."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import time

import validate_standalone_close as capture

ROOT = capture.ROOT
EXECUTABLE = ROOT / "bin/x64/Debug/RtPbrSurvey.exe"
INSPECTOR = ROOT / "out/build/sdk-free-exr/Debug/RtPbrSurvey.Mp4EncoderTests.exe"
FPS = 60
DURATION = 4.0
FRAME_PATTERN = re.compile(
    r"\[CAPTURE_FRAME\] index=(\d+) simulation=([\d.]+) render=(\d+) real=([\d.]+) dropped=(\d+)"
)
POSE_PATTERN = re.compile(
    r"\[CAPTURE_CAMERA_POSE\] index=(\d+) real=([\d.]+) "
    r"position=([-\d.,]+) gaze=([-\d.,]+)"
)
INPUT_PATTERN = re.compile(r"\[CAPTURE_CAMERA_INPUT\] real=([\d.]+) x=(-?\d+) y=(-?\d+)")
END_PATTERN = re.compile(
    r"\[CAPTURE_SESSION_END\] state=(\w+) accepted=(\d+) saved=(\d+) dropped=(\d+) elapsed=([\d.]+)"
)

WM_MOUSEMOVE = 0x0200
WM_RBUTTONDOWN = 0x0204
WM_RBUTTONUP = 0x0205
MK_RBUTTON = 0x0002


def post_mouse(hwnd, message, x, y, buttons=0):
    user = capture.ctypes.WinDLL("user32", use_last_error=True)
    user.PostMessageW.argtypes = [capture.ctypes.c_void_p, capture.ctypes.c_uint,
                                  capture.ctypes.c_size_t, capture.ctypes.c_ssize_t]
    lparam = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    if not user.PostMessageW(hwnd, message, buttons, lparam):
        raise capture.ctypes.WinError(capture.ctypes.get_last_error())


def parse_vector(value):
    return tuple(map(float, value.split(",")))


def run_case(output, delay_ms):
    name = "baseline" if delay_ms == 0 else "encoder-delay"
    directory = output / name
    directory.mkdir()
    log = directory / "d3d12.log"
    scene = json.loads(capture.SCENE.read_text(encoding="utf-8"))
    node = scene["nodes"][0]
    node["primitive"]["kind"] = "cube"
    node["primitive"]["size"] = 1.7
    node["scale"] = [1.2, 0.8, 0.5]
    node["translation"] = [0.6, 0, 0.1]
    node["rotation"] = [0, 0, 0, 1]
    scene["camera"]["position"] = [1, 0, -5]
    scene["environment"]["lightIntensity"] = 3
    scene_path = directory / "scene.json"
    scene_path.write_text(json.dumps(scene, indent=2), encoding="utf-8")

    preset = json.loads(capture.PRESET.read_text(encoding="utf-8"))
    preset["renderingPath"] = 1
    preset["camera"] = {"mode": "arcball", "yaw": math.atan2(1, -5), "pitch": 0,
                        "distance": math.sqrt(26), "pivot": [0, 0, 0], "fov": 55}
    preset["lighting"]["diffuseIblEnabled"] = True
    preset["lighting"]["specularIblEnabled"] = True
    preset["lighting"]["skyboxEnabled"] = True
    preset_path = directory / "render-preset.json"
    preset_path.write_text(json.dumps(preset, indent=2), encoding="utf-8")

    command = [str(EXECUTABLE), "-SceneFile", str(scene_path), "-RenderPreset", str(preset_path),
               "-CaptureSessionOutputDir", str(directory), "-CaptureSessionBaseName", "camera",
               "-CaptureSessionFormat", "mp4", "-CaptureSessionClock", "real-time", "-CaptureSessionFps",
               str(FPS), "-CaptureSessionDurationSeconds", str(DURATION), "-CaptureSessionWarmupFrames", "3",
               "-CaptureSessionRoi", "640", "300", "640", "480", "-CaptureSessionMp4BitrateMbps", "8",
               "-LogToFile", str(log), "-ExitAfterCapture"]
    if delay_ms:
        command += ["-CaptureSessionEncoderDelayMs", str(delay_ms)]

    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process_log = (directory / "process.log").open("w", encoding="utf-8")
    started = time.monotonic()
    child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup,
                             stdout=process_log, stderr=subprocess.STDOUT)
    try:
        hwnd = capture.wait_for(lambda: capture.window_for(child.pid), child)
        capture.wait_for(lambda: capture.frames(log), child)

        post_mouse(hwnd, WM_RBUTTONDOWN, 100, 100, MK_RBUTTON)
        for step in range(1, 31):
            post_mouse(hwnd, WM_MOUSEMOVE, 100 + step * 8, 100 + (step % 5) * 3, MK_RBUTTON)
            time.sleep(0.04)
        post_mouse(hwnd, WM_RBUTTONUP, 340, 112)
        time.sleep(0.15)
        post_mouse(hwnd, WM_RBUTTONDOWN, 340, 112, MK_RBUTTON)
        for step in range(24):
            post_mouse(hwnd, WM_MOUSEMOVE, 340 - step * 7, 112 - (step % 4) * 3, MK_RBUTTON)
            time.sleep(0.04)
        post_mouse(hwnd, WM_RBUTTONUP, 172, 112)

        code = child.wait(timeout=120)
        assert code == 0, f"Application exited with {code}"
        text = capture.read_log(log)
        frame_matches = FRAME_PATTERN.findall(text)
        pose_matches = POSE_PATTERN.findall(text)
        input_matches = INPUT_PATTERN.findall(text)
        end = END_PATTERN.search(text)
        assert end and end[1] == "Completed", "Capture session did not complete cleanly"
        frames = [(int(index), float(real), int(dropped)) for index, _, _, real, dropped in frame_matches]
        poses = [(int(index), float(real), parse_vector(position), parse_vector(gaze))
                 for index, real, position, gaze in pose_matches]
        assert len(frames) >= 3 and len(poses) == len(frames), "Camera/output diagnostics are incomplete"
        assert [frame[0] for frame in frames] == list(range(len(frames))), "Accepted output indexes are not contiguous"
        assert int(end[2]) == int(end[3]) == len(frames), "Accepted and saved output counts differ"
        assert int(end[4]) > 0, "The test did not exercise a dropped real-time capture interval"
        assert len(frames) + int(end[4]) == math.ceil(DURATION * FPS), \
            "Real-time duration did not account for every scheduled capture interval"
        assert len(input_matches) >= 10, "Automated camera drag did not reach the app"
        encoder = re.search(r"\[CAPTURE_ENCODER\] delay_ms=(\d+) max_append_ms=([\d.]+)", text)
        assert encoder and int(encoder[1]) == delay_ms, "Configured encoder delay is missing from diagnostics"
        assert float(encoder[2]) >= delay_ms * 0.95, "Configured encoder delay was not observed"

        baseline = poses[0][2] + poses[0][3]
        first_input = float(input_matches[0][0])
        changed_pose = next((pose for pose in poses if pose[1] > first_input and
                             max(abs(a - b) for a, b in zip(pose[2] + pose[3], baseline)) > 1e-4), None)
        assert changed_pose, "Captured camera pose did not respond to mouse input"
        response_latency = changed_pose[1] - first_input
        assert 0.0 <= response_latency < 0.75, "Camera response exceeded the validation latency bound"
        response_latencies = [next(pose[1] - float(event[0]) for pose in poses if pose[1] > float(event[0]))
                              for event in input_matches if any(pose[1] > float(event[0]) for pose in poses)]
        assert len(response_latencies) >= len(input_matches) - 2, "Camera input response samples are incomplete"
        response_p95 = sorted(response_latencies)[math.ceil(len(response_latencies) * 0.95) - 1]
        response_max = max(response_latencies)
        assert response_max < 0.75, "A camera input waited too long for a captured output pose"
        assert any(max(abs(a - b) for a, b in zip(poses[index][2] + poses[index][3],
                                                   poses[index - 1][2] + poses[index - 1][3])) > 1e-4
                   for index in range(1, len(poses))), "Camera motion was absent from captured outputs"

        timeline = directory / "expected-timestamps.txt"
        first_time = frames[0][1]
        timeline.write_text("".join(f"{real - first_time:.9f}\r\n" for _, real, _ in frames),
                            encoding="utf-8", newline="")
        video = directory / "camera.mp4"
        inspector_command = [str(INSPECTOR), "--timeline", str(video), "640", "480", str(FPS),
                             str(DURATION), str(timeline)]
        inspected = subprocess.run(inspector_command, capture_output=True, text=True)
        (directory / "decode.log").write_text(inspected.stdout + inspected.stderr, encoding="utf-8")
        assert inspected.returncode == 0, inspected.stdout + inspected.stderr

        pipeline_end = re.search(
            r"\[CAPTURE_PIPELINE_END\] accepted=(\d+) saved=(\d+) encoder_pending=(\d+) "
            r"encoder_bytes=(\d+) gpu_pending=(\d+) readback_bytes=(\d+)", text)
        pipeline_values = list(map(int, pipeline_end.groups())) if pipeline_end else []
        readback_budget = 2 * ((640 * 4 + 255) // 256 * 256) * 480
        assert pipeline_values[:5] == [len(frames), len(frames), 0, 0, 0] and \
            0 <= pipeline_values[5] <= readback_budget, "Capture resources did not drain within the readback budget"
        return {"name": name, "encoderDelayMilliseconds": delay_ms, "exitCode": code,
                "command": command, "acceptedFrames": len(frames), "savedFrames": int(end[3]),
                "droppedIntervals": int(end[4]), "mediaDurationSeconds": DURATION,
                "firstInputRealSeconds": first_input, "firstChangedOutputRealSeconds": changed_pose[1],
                "cameraResponseLatencySeconds": response_latency, "cameraInputEvents": len(input_matches),
                "cameraResponseP95Seconds": response_p95, "cameraResponseMaxSeconds": response_max,
                "maxRealFrameIntervalSeconds": max(b[1] - a[1] for a, b in zip(frames, frames[1:])),
                "readbackBytesAfterDrain": pipeline_values[5],
                "completionWallSeconds": time.monotonic() - started,
                "maxAppendMilliseconds": float(encoder[2]),
                "decoder": inspected.stdout.strip(), "d3d12Errors": capture.classify_errors(text)[0],
                "savedVideoSha256": hashlib.sha256(video.read_bytes()).hexdigest()}
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        process_log.close()


def main():
    global EXECUTABLE, INSPECTOR
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--executable", type=Path, default=EXECUTABLE)
    parser.add_argument("--inspector", type=Path, default=INSPECTOR)
    args = parser.parse_args()
    EXECUTABLE = args.executable.resolve()
    INSPECTOR = args.inspector.resolve()
    sys.path.insert(0, str(ROOT / "bin/CapturePort/python-packages"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    records = []
    for delay in (0, 100):
        record = run_case(output, delay)
        records.append(record)
        report = {"testedCommit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "workingTreeDirty": bool(subprocess.check_output(
                ["git", "status", "--porcelain"], cwd=ROOT, text=True).strip()),
            "sourceSha256": {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in
                             ("App/RtPbrSurveyApp.cpp", "Runtime/CaptureSession.cpp",
                              "Tests/CaptureSession/validate_realtime_camera_pressure.py",
                              "Tests/Mp4EncoderTests.cpp")},
            "executableSha256": hashlib.sha256(EXECUTABLE.read_bytes()).hexdigest(),
            "inspectorSha256": hashlib.sha256(INSPECTOR.read_bytes()).hexdigest(), "records": records}
        (output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(record), flush=True)
    print("Interactive real-time camera pressure cases passed.", flush=True)


if __name__ == "__main__":
    main()
