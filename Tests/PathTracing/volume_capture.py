"""Shared strict capture contract for the Native PT volume unit scenes."""
import json
from pathlib import Path
import subprocess

from transmission_texture_fixture import ROOT
from validate_inputs import read_buffer
from validate_part1 import sha


def capture_volume(exe, folder, frames, resource="SceneColor", nrd=False):
    folder = Path(folder)
    scene = json.loads((folder/"scene.json").read_text(encoding="utf-8"))
    preset = json.loads((folder/scene["renderPreset"]).read_text(encoding="utf-8"))["pathTracing"]
    buffer, log = folder/"capture.ptbuf", folder/"debug.log"
    command = [str(exe), "-SceneFile", str(folder/"scene.json"), "-EnablePathTracing", "-DebugPreviewResource",
        "PathTracing."+resource, "-CapturePath", str(buffer), "-CaptureAfterFrames", str(frames),
        "-LogToFile", str(log), "-ExitAfterCapture"]
    if nrd:
        command.append("-NrdPreview")
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    child = subprocess.Popen(command, cwd=ROOT, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        code = child.wait(timeout=180)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait()
        raise ValueError("Volume capture timeout: "+folder.name)
    text = log.read_text(encoding="utf-8-sig")
    records = [json.loads(line[len("[PathTracing] "):]) for line in text.splitlines() if line.startswith("[PathTracing] ")]
    if code or "[ERROR]" in text or "[CORRUPTION]" in text or len(records) != 1:
        raise ValueError("Runtime/Debug Layer failure: "+folder.name)
    state = records[0]
    meta, image = read_buffer(buffer)
    if (meta.get("volumeTransmissionActive") is not True or meta.get("pathTracingDebugOutput") != preset["debugOutput"]
        or meta.get("resource") != "PathTracing."+resource or meta.get("sampleStartIndex") != frames
        or meta.get("primarySampling") != "pixel-center" or meta.get("samplesPerFrame") != 1
        or state.get("scene") != scene["name"] or state.get("samplesPerFrame") != 1
        or state.get("maxBounces") != preset["maxBounces"] or state.get("accumulatedSamples") != frames
        or state.get("pixelCenterPrimary") is not True or state.get("environmentEnabled") is not False
        or state.get("nrdPreviewRequested") is not nrd or meta.get("nrdPreviewRequested") is not nrd
        or state.get("d3d12DebugLayerEnabledByApp") is not True):
        raise ValueError("Volume runtime state mismatch: "+folder.name)
    if nrd and (meta.get("nrdPresentation") != "Native fallback: volume transmission is not qualified for NRD"
        or meta.get("nrdLastEvaluation", {}).get("recorded") is not False
        or state.get("nrdPlannedDispatches") != 0 or state.get("nrdPreparedDispatches") != 0):
        raise ValueError("Volume did not bypass NRD")
    evidence = dict(command=command, metadata=meta, runtime=state,
        inputs={str(p.relative_to(folder)): sha(p) for p in folder.iterdir() if p.suffix in (".json", ".gltf", ".png")},
        bufferSha256=sha(buffer), logSha256=sha(log))
    return evidence, image
