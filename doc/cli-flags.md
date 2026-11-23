# CLI Flags for Automation

Flags are parsed in `RtPbrSurveyApp::ParseCommandLineArgs()`. All flags are optional; paths are relative to the repository root unless specified otherwise.

## Core Flags

| Flag | Argument | Description |
|------|----------|-------------|
| `-LogToFile` | Log file path, e.g. `d3d12_debug.log` | On startup, dump accumulated D3D12 Debug Layer messages via `ID3D12InfoQueue` to the specified file. Also append new messages as they arrive when polled each frame. |
| `-LogFPS` | Frame interval, e.g. `60` | Log CPU FPS to the same log file every N frames. Only active when `-LogToFile` is also specified. Value is `1000.0f / cpuFrameTimeMs`. |
| `-AutoSelectGltfDamagedHelmet` | None | On startup, automatically select the `glTF Viewer > DamagedHelmet` scene and switch to Running mode without user interaction. |
| `-EnableDlssSr` | None | Enable DLSS Super Resolution at startup. Forces Deferred rendering and the final Light Pass view. |
| `-DlssQuality` | `dlaa`, `quality`, `balanced`, `performance`, or `ultra-performance` | Enable DLSS SR and select the quality mode. |
| `-CapturePath` | PNG path, e.g. `Screenshots\\validation.png` | Queue one PNG capture through the existing renderer screenshot path. |
| `-CaptureAfterFrames` | Frame count, e.g. `60` | Render this many warm-up frames before queuing the CLI screenshot. Used with `-CapturePath`. |
| `-ExitAfterCapture` | None | Close the application after the CLI screenshot succeeds or fails. Used with `-CapturePath`. |
| `-EnableDlssRayReconstruction` | None | Enable the experimental DLSS Ray Reconstruction render-graph path for automation. Native RR evaluate still requires the separate native flag. |
| `-EnableExperimentalNativeRayReconstruction` | None | Opt into the guarded native DLSS Ray Reconstruction evaluate call. Implies `-EnableDlssRayReconstruction`; specify both flags in validation commands for clarity. |

## Hybrid Reflection Validation

The reflection capture flags are documented separately in [hybrid-reflection-validation.md](hybrid-reflection-validation.md).
