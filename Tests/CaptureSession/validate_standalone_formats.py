"""Decode real GPU captures, including H.264 MP4; keep artifacts under bin."""
import argparse
import hashlib
import json
import re
import math
from pathlib import Path
import subprocess
import sys

import validate_standalone_close as capture

ROOT = capture.ROOT
INSPECTOR = ROOT / "build/scene-document-tests/Debug/RtPbrSurvey.Mp4EncoderTests.exe"
VALIDATE_READBACK_POOL = False


def validate_readback_pool(text, case, accepted):
    pattern = (r"\[CAPTURE_PIPELINE(_END)?\] accepted=(\d+) saved=(\d+) encoder_pending=(\d+) "
               r"encoder_bytes=(\d+) gpu_pending=(\d+) readback_bytes=(\d+) "
               r"readback_allocations=(\d+) readback_reuses=(\d+)")
    samples = re.findall(pattern, text)
    assert samples and samples[-1][0] == "_END", "No final readback-pool diagnostics"
    numeric = [list(map(int, sample[1:])) for sample in samples]
    width, height = case.get("roi", (0, 0, 1920, 1080))[2:]
    gpu_budget = 2 * ((width * 4 + 255) // 256 * 256) * height
    assert all(sample[2] <= 2 and sample[4] <= 2 and sample[3] <= 128 * 1024 * 1024
               and sample[5] <= gpu_budget for sample in numeric), "Capture pool exceeded its budget"
    final = numeric[-1]
    assert final[0] == final[1] == len(accepted), "Accepted frames did not drain"
    assert final[2:5] == [0, 0, 0], "GPU or encoder capacity leaked after completion"
    assert 1 <= final[6] <= 2, "Readbacks were allocated per frame rather than per slot"
    assert final[6] + final[7] == len(accepted) and final[7] > 0, "Readback reuse was not observed"
    return {"maxReservedFrames": max(sample[2] for sample in numeric),
            "maxReservedBytes": max(sample[3] for sample in numeric),
            "maxGpuPending": max(sample[4] for sample in numeric),
            "readbackBytes": final[5], "allocations": final[6], "reuses": final[7]}


def validate_pt_history(text, case, camera_samples):
    pattern = (r'\[CAPTURE_PT\] render=(\d+) output=(-?\d+) advanced=(\d+) mode=(\w+) before_samples=(\d+) '
               r'samples=(\d+) before_index=(\d+) sample_index=(\d+) history=(\d+) reset="([^"]+)" spp=(\d+) '
               r'position=([-\d.,]+)')
    records = re.findall(pattern, text)
    assert records, "No rendered PT capture diagnostics"
    encoder = re.search(r'\[CAPTURE_ENCODER\] delay_ms=(\d+) max_append_ms=([\d.]+)', text)
    assert encoder and int(encoder[1]) == case["encoderDelay"] and float(encoder[2]) >= case["encoderDelay"] * 0.95, "Diagnostic encoder delay was not observed"
    waits, drains, outputs = 0, 0, []
    previous = None
    for record in records:
        render, output, advanced, mode, before_samples, samples, before_index, index, history, reset, spp, position = record
        output, advanced, before_samples, samples, before_index, index, history, spp = map(
            int, (output, advanced, before_samples, samples, before_index, index, history, spp))
        assert spp == 4, "PT history fixture did not use four samples/update"
        if not advanced:
            assert samples == before_samples and index == before_index, "PT advanced during a presentation-only wait"
            if previous:
                assert (before_samples, before_index, history) == previous[:3], "PT state changed before wait-frame submission"
                assert max(abs(a - b) for a, b in zip(map(float, position.split(",")), previous[3])) < 2e-5, "Camera moved during output backpressure"
            waits += mode == "CapacityWait"
            drains += mode == "DrainOnly"
        elif output >= 0 and case["orbit"] != 0 and output > 0:
            assert reset == "Camera" and samples == spp and index == spp and history == 1, "Moving pose retained old PT history"
        else:
            assert samples == before_samples + spp and index == before_index + spp and history == 1, "Stationary PT did not accumulate"
        if output >= 0:
            outputs.append(output)
            assert advanced == 1, "PT output was accepted without rendering its pose"
            actual = tuple(map(float, position.split(",")))
            expected = tuple(map(float, camera_samples[output][5].split(",")))
            assert max(abs(a - b) for a, b in zip(actual, expected)) < 2e-5, "Rendered CPU camera differs from output pose"
        previous = (samples, index, history, tuple(map(float, position.split(","))))
    assert outputs == list(range(case["frames"])), "Output-to-PT frame mapping is incomplete"
    assert waits > 0 and drains > 0, "Injected pressure did not exercise capacity wait and drain"
    return {"physicalFrames": len(records), "outputs": len(outputs), "capacityWaitFrames": waits, "drainFrames": drains,
            "maxAppendMilliseconds": float(encoder[2])}


def validate_outputs(directory, case, accepted):
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
        from PIL import Image
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
            if case.get("orbit", 0) != 0:
                command.insert(1, "--motion")
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
        if "encoderDelay" in case:
            command += ["-CaptureSessionEncoderDelayMs", str(case["encoderDelay"])]
    if case["format"] == "gif":
        command += ["-CaptureSessionGifRepeat", "2"]
    if case.get("duration"):
        command += ["-CaptureSessionDurationSeconds", str(case["duration"])]
    if "orbit" in case:
        command += ["-CaptureSessionOrbitDegreesPerSecond", str(case["orbit"])]
        scene = json.loads(capture.SCENE.read_text(encoding="utf-8"))
        node = scene["nodes"][0]
        node["primitive"]["kind"] = "cube"
        node["primitive"]["size"] = 1.7
        node["scale"] = [1.2, 0.8, 0.5]
        node["translation"] = [0.6, 0, 0.1]
        node["rotation"] = [0, 0, 0, 1]
        scene["camera"]["position"] = [1, 0, -5]
        scene["environment"]["lightIntensity"] = 3
        scene_path = directory / "orbit-scene.json"
        scene_path.write_text(json.dumps(scene, indent=2), encoding="utf-8")
        command[command.index("-SceneFile") + 1] = str(scene_path)
    if "renderingPath" in case or "orbit" in case:
        preset = json.loads(capture.PRESET.read_text(encoding="utf-8"))
        preset["renderingPath"] = case.get("renderingPath", 1)
        if "orbit" in case:
            preset["camera"] = {"mode": "arcball", "yaw": math.atan2(1, -5), "pitch": 0,
                                "distance": math.sqrt(26), "pivot": [0, 0, 0], "fov": 55}
            preset["lighting"]["diffuseIblEnabled"] = True
            preset["lighting"]["specularIblEnabled"] = True
            preset["lighting"]["skyboxEnabled"] = True
            if case.get("ptHistory"):
                preset["pathTracing"]["samplesPerFrame"] = 4
                preset["pathTracing"]["accumulate"] = True
        preset_path = directory / "render-preset.json"
        preset_path.write_text(json.dumps(preset, indent=2), encoding="utf-8")
        command[command.index("-RenderPreset") + 1] = str(preset_path)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process_log = (directory / "process.log").open("w", encoding="utf-8")
    working_directory = ROOT if case.get("rootCwd") else directory
    child = subprocess.Popen(command, cwd=working_directory, startupinfo=startup,
                             stdout=process_log, stderr=subprocess.STDOUT)
    try:
        code = child.wait(timeout=case.get("timeout", 150))
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
        if "orbit" in case:
            samples = re.findall(r"\[CAPTURE_CAMERA\] index=(\d+) time=([-\d.]+) yaw=([-\d.]+) "
                                 r"pitch=([-\d.]+) distance=([-\d.]+) position=([-\d.,]+) pivot=([-\d.,]+)", text)
            assert len(samples) == len(accepted), "Camera sample count differs from accepted output count"
            first_yaw = float(samples[0][2])
            initial_position = tuple(map(float, samples[0][5].split(",")))
            assert max(abs(a - b) for a, b in zip(initial_position, (1, 0, -5))) < 2e-5, "Orbit replaced its starting view"
            for frame, sample in enumerate(samples):
                index, time, yaw, pitch, distance, position, pivot = sample
                assert int(index) == frame and abs(float(time) - frame / case["fps"]) < 2e-9
                expected = first_yaw + math.radians(case["orbit"]) * frame / case["fps"]
                assert abs(math.remainder(float(yaw) - expected, 2 * math.pi)) < 2e-6, "Orbit pose skipped/repeated"
                x, y, z = map(float, position.split(","))
                px, py, pz = map(float, pivot.split(","))
                pitch, distance = float(pitch), float(distance)
                expected_position = (px + distance * math.cos(pitch) * math.sin(float(yaw)),
                                     py + distance * math.sin(pitch),
                                     pz + distance * math.cos(pitch) * math.cos(float(yaw)))
                assert max(abs(a - b) for a, b in zip((x, y, z), expected_position)) < 2e-5
            if case["format"] == "png" and case["orbit"] != 0:
                from PIL import Image, ImageChops, ImageStat
                images = outputs["files"]
                with Image.open(directory / images[0]) as first, Image.open(directory / images[-1]) as last:
                    difference = ImageStat.Stat(ImageChops.difference(first.convert("RGB"), last.convert("RGB")))
                    assert max(difference.mean) > 1, "Orbit PNG output did not change"
            outputs["cameraSamples"] = samples
            if case.get("ptHistory"):
                outputs["ptHistory"] = validate_pt_history(text, case, samples)
        if VALIDATE_READBACK_POOL and case["format"] == "mp4":
            outputs["readbackPool"] = validate_readback_pool(text, case, accepted)
        if "encoderDelay" in case:
            encoder = re.search(r"\[CAPTURE_ENCODER\] delay_ms=(\d+) max_append_ms=([\d.]+)", text)
            assert encoder and int(encoder[1]) == case["encoderDelay"], \
                "Diagnostic encoder delay was not configured"
            assert float(encoder[2]) >= case["encoderDelay"] * 0.95, "Diagnostic encoder delay was not observed"
            outputs["encoderDelay"] = {"configuredMilliseconds": int(encoder[1]),
                                       "maxAppendMilliseconds": float(encoder[2])}
        return {"name": case["name"], "command": command, "exitCode": code,
                "captureFrames": accepted, "d3d12Errors": errors, **outputs}
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        process_log.close()


def main():
    global INSPECTOR, VALIDATE_READBACK_POOL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cases", help="Comma-separated case names")
    parser.add_argument("--inspector", type=Path, default=INSPECTOR)
    parser.add_argument("--executable", type=Path, default=capture.EXECUTABLE)
    parser.add_argument("--validate-readback-pool", action="store_true")
    args = parser.parse_args()
    INSPECTOR = args.inspector.resolve()
    VALIDATE_READBACK_POOL = args.validate_readback_pool
    capture.EXECUTABLE = args.executable.resolve()
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
        dict(name="mp4-full-long", format="mp4", fps=30, frames=1800, timeout=2400),
        dict(name="mp4-roi-clock-long", format="mp4", fps=60, frames=1800, timeout=240,
             roi=(100, 100, 64, 64)),
        dict(name="mp4-orbit-long-pressure", format="mp4", fps=60, frames=1800, orbit=37,
             encoderDelay=100, timeout=1200, roi=(640, 300, 640, 480), rootCwd=True),
        dict(name="png-orbit", format="png", fps=30, frames=12, orbit=90, roi=(640, 300, 640, 480)),
        dict(name="mp4-orbit", format="mp4", fps=30, frames=12, orbit=90, roi=(640, 300, 640, 480)),
        dict(name="mp4-orbit-pt", format="mp4", fps=30, frames=12, orbit=90, renderingPath=2,
             roi=(640, 300, 640, 480)),
        dict(name="mp4-orbit-pt-history", format="mp4", fps=30, frames=12, orbit=90, renderingPath=2,
             roi=(640, 300, 640, 480), encoderDelay=100, ptHistory=True),
        dict(name="mp4-static-pt-history", format="mp4", fps=30, frames=12, orbit=0, renderingPath=2,
             roi=(640, 300, 640, 480), encoderDelay=100, ptHistory=True),
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
    else:
        cases = [case for case in cases if case["name"] not in
                 ("mp4-full-long", "mp4-roi-clock-long", "mp4-orbit-long-pressure")]
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
        console_record = {key: value for key, value in record.items()
                          if key not in ("captureFrames", "cameraSamples")}
        console_record["captureFrameCount"] = len(record["captureFrames"])
        if "cameraSamples" in record:
            console_record["cameraSampleCount"] = len(record["cameraSamples"])
        print(json.dumps(console_record), flush=True)
    print(f"All {len(cases)} GPU format cases passed.", flush=True)


if __name__ == "__main__":
    main()
