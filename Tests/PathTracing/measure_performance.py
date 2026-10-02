"""Measure completed PathTracingPass GPU timestamps without changing the renderer."""
import argparse
import copy
import csv
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import re
import statistics
import subprocess
import threading
import time

from validate_part1 import write_json

ROOT = Path(__file__).resolve().parents[2]
BASE = dict(width=1920, height=1080, samplesPerFrame=1, maxBounces=2, lights=1, stacks=24, slices=32)


def cases():
    result = [dict(name="baseline", axis="baseline", **BASE)]
    for axis, changes in (
        ("resolution", [dict(width=960, height=540), dict(width=1280, height=720)]),
        ("samplesPerFrame", [dict(samplesPerFrame=2), dict(samplesPerFrame=4)]),
        ("maxBounces", [dict(maxBounces=1), dict(maxBounces=4)]),
        ("lights", [dict(lights=4), dict(lights=8)]),
        ("geometry", [dict(stacks=12, slices=16), dict(stacks=96, slices=128)])):
        for index, change in enumerate(changes):
            result.append(dict(BASE, name=f"{axis}-{index}", axis=axis, **change))
    return result


def percentile(values, probability):
    if not values or not 0 <= probability <= 1 or any(not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError("Need finite positive GPU times and a probability in [0,1]")
    ordered = sorted(values)
    position = (len(ordered)-1)*probability
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper]-ordered[lower])*(position-lower)


def summarize(values):
    return dict(count=len(values), medianMs=percentile(values, .5), p90Ms=percentile(values, .9),
        p95Ms=percentile(values, .95), meanMs=statistics.mean(values), minMs=min(values), maxMs=max(values))


def parse_timing_log(text):
    rows = []
    frame = None
    for line in text.splitlines():
        if line.startswith("[PathTracing] "):
            break  # capture queued: never include capture/readback observations
        match = re.match(r"\[GPU\] Frame (\d+):", line)
        if match:
            frame = int(match[1])
        match = re.match(r"\[GPU Pass\] PathTracingPass: (\S+) ms$", line)
        if match:
            value = float(match[1])
            if frame is None or not math.isfinite(value) or value <= 0:
                raise ValueError("Missing GPU frame or invalid PathTracingPass timing")
            rows.append(dict(cpuLogFrame=frame, gpuMs=value))
    if len({r["cpuLogFrame"] for r in rows}) != len(rows):
        raise ValueError("Duplicate timing observation for a logged frame")
    return rows


def make_fixture(case):
    directory = ROOT / "Assets/Scenes/PathTracingValidation/roughness"
    scene = json.loads((directory / "scene.json").read_text())
    preset = json.loads((directory / "render-preset.json").read_text())
    light = json.loads((ROOT / "Assets/Scenes/PathTracingValidation/single-light-visibility/render-preset.json").read_text())["lighting"]["lights"][0]
    lights = []
    for index in range(case["lights"]):
        entry = copy.deepcopy(light)
        entry.update(id=index+1, name=f"Perf light {index+1}", intensity=8/case["lights"])
        # Identical co-located lights keep incident illumination constant across counts.
        lights.append(entry)
    preset["lighting"].update(lights=lights, primaryShadowLightId=1, directLightEnabled=True)
    preset["pathTracing"].update(samplesPerFrame=case["samplesPerFrame"], maxBounces=case["maxBounces"],
        directLightingEnabled=True, randomSeed=7)
    for node in scene["nodes"]:
        if node["primitive"]["kind"] == "sphere":
            node["primitive"].update(stacks=case["stacks"], slices=case["slices"])
    scene.update(sceneId="perf-"+case["name"], name="PT performance "+case["name"])
    return scene, preset


def power_state():
    class Power(ctypes.Structure):
        _fields_ = [("ac", wintypes.BYTE), ("batteryFlags", wintypes.BYTE), ("batteryPercent", wintypes.BYTE),
            ("systemStatus", wintypes.BYTE), ("batteryLife", wintypes.DWORD), ("batteryFullLife", wintypes.DWORD)]
    state = Power()
    ok = ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(state))
    result = dict(acLineStatus=state.ac if ok else None, batteryPercent=state.batteryPercent if ok else None)
    for key, command in (
        ("powerScheme", ["powercfg", "/getactivescheme"]),
        ("gpu", ["nvidia-smi", "--query-gpu=name,driver_version,pstate,temperature.gpu,clocks.sm,clocks.mem,power.draw,power.limit", "--format=csv,noheader"])):
        run = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
        result[key] = dict(exitCode=run.returncode, stdout=run.stdout.strip(), stderr=run.stderr.strip())
    return result


def monitor_gpu(stop, log_path, observations):
    while not stop.is_set():
        run = subprocess.run(["nvidia-smi", "--query-gpu=timestamp,pstate,temperature.gpu,clocks.sm,clocks.mem,power.draw",
            "--format=csv,noheader"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10)
        text = log_path.read_text(encoding="utf-8-sig", errors="replace") if log_path.exists() else ""
        frames = re.findall(r"\[GPU\] Frame (\d+):", text)
        observations.append(dict(utc=datetime.now(timezone.utc).isoformat(),
            latestLoggedCpuFrame=int(frames[-1]) if frames else None, exitCode=run.returncode,
            gpu=run.stdout.strip(), error=run.stderr.strip()))
        stop.wait(.5)


def resize_own_window(process, width, height):
    user = ctypes.windll.user32
    user.GetWindowDpiAwarenessContext.argtypes = [wintypes.HWND]
    user.GetWindowDpiAwarenessContext.restype = ctypes.c_void_p
    user.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    user.SetThreadDpiAwarenessContext.restype = ctypes.c_void_p
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetWindowLongPtrW.restype = ctypes.c_ssize_t
    user.SetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_ssize_t]
    user.SetWindowLongPtrW.restype = ctypes.c_ssize_t
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    deadline = time.monotonic()+30
    while time.monotonic() < deadline:
        windows = []
        @callback_type
        def visit(handle, parameter):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
            rect = wintypes.RECT()
            if pid.value == process.pid and user.GetClientRect(handle, ctypes.byref(rect)) and rect.right > 100:
                windows.append(handle)
            return True
        user.EnumWindows(visit, 0)
        if windows:
            handle = windows[0]
            # Match the application's coordinate space under display DPI scaling.
            previous_dpi = user.SetThreadDpiAwarenessContext(user.GetWindowDpiAwarenessContext(handle))
            # Borderless test window avoids the monitor's maximum tracking size
            # subtracting the title bar from a full-height requested client area.
            style = user.GetWindowLongPtrW(handle, -16)
            user.SetWindowLongPtrW(handle, -16, style & ~0x00CF0000)
            user.SetWindowPos(handle, None, 0, 0, width, height, 0x0002 | 0x0004 | 0x0010 | 0x0020)
            client, outer = wintypes.RECT(), wintypes.RECT()
            user.GetClientRect(handle, ctypes.byref(client))
            user.GetWindowRect(handle, ctypes.byref(outer))
            if not user.SetWindowPos(handle, None, 0, 0, width+(outer.right-outer.left-client.right),
                height+(outer.bottom-outer.top-client.bottom), 0x0002 | 0x0004 | 0x0010):
                raise OSError("SetWindowPos failed")
            for attempt in range(40):
                user.GetClientRect(handle, ctypes.byref(client))
                if (client.right, client.bottom) == (width, height):
                    break
                if attempt in (5, 15, 25):
                    user.GetWindowRect(handle, ctypes.byref(outer))
                    user.SetWindowPos(handle, None, 0, 0,
                        outer.right-outer.left+width-client.right,
                        outer.bottom-outer.top+height-client.bottom, 0x0002 | 0x0004 | 0x0010)
                time.sleep(.05)
            if (client.right, client.bottom) != (width, height):
                raise ValueError(f"Client size {client.right}x{client.bottom} differs from {width}x{height}")
            user.SetThreadDpiAwarenessContext(previous_dpi)
            return dict(pid=process.pid, clientWidth=client.right, clientHeight=client.bottom)
        if process.poll() is not None:
            raise RuntimeError("Application exited before its window was created")
        time.sleep(.05)
    raise TimeoutError("Application window was not created")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--cases", nargs="+", choices=[r["name"] for r in cases()])
    parser.add_argument("--warmup", type=int, default=60)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if min(args.warmup, args.frames, args.repeats) <= 0:
        parser.error("Warm-up, measurement frames and repeats must be positive")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Use an empty output directory")
    output.mkdir(parents=True, exist_ok=True)
    selected = cases()[:3] if args.smoke else cases()
    if args.cases:
        selected = [r for r in cases() if r["name"] in args.cases]
    report = dict(schemaVersion=1, part=4, generatedUtc=datetime.now(timezone.utc).isoformat(),
        baseCommit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        branch=subprocess.check_output(["git", "branch", "--show-current"], cwd=ROOT, text=True).strip(),
        workspace=str(ROOT), build="Debug x64", warmupObservations=args.warmup,
        measurementObservations=args.frames, repeats=args.repeats, seed=7, cases=selected, runs=[], failures=[], status="running",
        timing="Latest completed GPU PathTracingPass duration logged once per CPU frame; no CPU FPS substitution",
        initialPower=power_state())
    report["exeSha256"] = hashlib.sha256((ROOT / "bin/x64/Debug/RtPbrSurvey.exe").read_bytes()).hexdigest()
    report["rendererBuildCommit"] = report["baseCommit"]
    write_json(output / "report.json", report)
    schedule = []
    for repeat in range(args.repeats):
        order = selected[:]
        random.Random(84+repeat).shuffle(order)
        schedule.extend((repeat, case) for case in order)
    for repeat, case in schedule:
        name = f"{case['name']}-r{repeat}"
        scene, preset = make_fixture(case)
        scene_path, preset_path = output / (name+"-scene.json"), output / (name+"-preset.json")
        scene["renderPreset"] = preset_path.name
        write_json(scene_path, scene)
        write_json(preset_path, preset)
        capture_frame = args.warmup+args.frames+16
        command = [str(ROOT / "bin/x64/Debug/RtPbrSurvey.exe"), "-SceneFile", str(scene_path), "-RenderPreset", str(preset_path),
            "-EnablePathTracing", "-PathTracingSeed", "7", "-PathTracingEnvironmentMode", "5",
            "-CaptureAfterFrames", str(capture_frame), "-LogFPS", "1", "-LogToFile", str(output / (name+".log")),
            "-CapturePath", str(output / (name+".pfm")), "-ExitAfterCapture"]
        record = dict(name=name, case=case["name"], repeat=repeat, command=command, powerBefore=power_state(),
            sceneSha256=hashlib.sha256(scene_path.read_bytes()).hexdigest(), presetSha256=hashlib.sha256(preset_path.read_bytes()).hexdigest())
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        process = subprocess.Popen(command, cwd=ROOT, startupinfo=startup)
        telemetry = []
        stop = threading.Event()
        monitor = threading.Thread(target=monitor_gpu, args=(stop, output / (name+".log"), telemetry), daemon=True)
        monitor.start()
        try:
            record["window"] = resize_own_window(process, case["width"], case["height"])
            if process.wait(timeout=240) != 0:
                raise RuntimeError("Application returned a failure")
            text = (output / (name+".log")).read_text(encoding="utf-8-sig")
            if any(line.startswith(("[ERROR]", "[CORRUPTION]")) for line in text.splitlines()):
                raise RuntimeError("D3D12 error in capture log")
            diagnostics = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines() if line.startswith("[PathTracing] ")][-1]
            for key, expected in dict(renderWidth=case["width"], renderHeight=case["height"],
                samplesPerFrame=case["samplesPerFrame"], maxBounces=case["maxBounces"], randomSeed=7, targetSamples=0).items():
                if diagnostics[key] != expected:
                    raise ValueError(f"Unexpected {key}: {diagnostics[key]} != {expected}")
            if not diagnostics["gpuTimingAvailable"]:
                raise ValueError("GPU timestamps unavailable")
            if diagnostics["accumulatedSamples"] < (args.warmup+args.frames)*case["samplesPerFrame"]:
                raise ValueError("Accumulation was reset during the measurement window")
            rows = parse_timing_log(text)
            measured = rows[args.warmup:args.warmup+args.frames]
            if len(measured) != args.frames:
                raise ValueError("Not enough valid GPU observations")
            with (output / (name+".csv")).open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=["cpuLogFrame", "gpuMs"])
                writer.writeheader()
                writer.writerows(measured)
            record.update(diagnostics=diagnostics, stats=summarize([r["gpuMs"] for r in measured]),
                firstMeasuredCpuFrame=measured[0]["cpuLogFrame"], lastMeasuredCpuFrame=measured[-1]["cpuLogFrame"],
                totalValidObservations=len(rows), values=[r["gpuMs"] for r in measured], powerAfter=power_state())
            report["runs"].append(record)
            print(name, json.dumps(record["stats"]), flush=True)
        except Exception as error:
            report["failures"].append(dict(name=name, command=command, error=str(error)))
            report["status"] = "failed"
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            stop.set()
            monitor.join(timeout=12)
            record["gpuTelemetry"] = telemetry
            if "firstMeasuredCpuFrame" in record:
                record["measurementGpuTelemetry"] = [r for r in telemetry if r["latestLoggedCpuFrame"] is not None
                    and record["firstMeasuredCpuFrame"] <= r["latestLoggedCpuFrame"] <= record["lastMeasuredCpuFrame"]]
            write_json(output / "report.json", report)
    report["aggregates"] = [dict(case=case["name"], **summarize([v for r in report["runs"] if r["case"] == case["name"] for v in r["values"]])) for case in selected]
    report["status"] = "complete"
    report["finalPower"] = power_state()
    write_json(output / "report.json", report)


if __name__ == "__main__":
    main()
