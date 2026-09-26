# Capture Session Phase 1

Phase 1 adds a host-public, GPU-wait-free scheduler in `Runtime/CaptureSession.h` and a thin `SceneRenderer` bridge. It is intentionally limited to image sequences:

- PNG captures the final composed output, including ImGui.
- EXR captures linear pre-tone-map scene color, excluding ImGui.
- GIF and MP4 are recognized output formats but fail `Start` explicitly until their encoder phases land.

## Host contract

The host supplies render-frame, real-time, and simulation-time values through `CaptureSessionTiming`. `Update` may expose one `ScreenshotRequest`; the host acquires it, submits it, calls `MarkRequestAccepted`, and reports the asynchronous `ScreenshotResult` through `CompleteRequest`.

Only one session request is in flight. This deliberately matches the renderer readback path and prevents an unbounded stale capture queue.

- Real-time sessions advance their deadline while busy and increment `droppedFrameCount` for each missed deadline.
- Fixed-step sessions never drop a required frame. When a request is ready, `CanAdvanceFixedStep` is false until the host accepts it; an in-flight readback does not cause a GPU wait.
- `Stop` accepts no new request, drains the in-flight result, then reaches `Completed`. A failed output reaches `Failed` after the same cleanup boundary.

Output names are ordered as `<baseName>_000000.<extension>`. A `singleOutputPath` exists only to permit a future exact-file legacy translation; the existing `-CapturePath` behavior remains unchanged in Phase 1.

## Standalone CLI

The standalone app accepts a separate Capture Session command family and keeps existing screenshot automation compatible:

```text
-CaptureSessionOutputDir <directory>
-CaptureSessionBaseName <base-name>
-CaptureSessionFormat png|exr|gif|mp4
-CaptureSessionFrames <count>
-CaptureSessionFps <frames-per-second>
-CaptureSessionWarmupFrames <count>
-CaptureSessionClock real-time|fixed-step
-ExitAfterCapture
```

`-CaptureSessionOutputDir`, `-CaptureSessionBaseName`, and `-CaptureSessionFrames` are required. Capture Session is mutually exclusive with `-CapturePath` and Reflection capture plans because those paths consume the same renderer screenshot-result stream.

The reusable ImGui panel, GIF encoder, and MP4 encoder remain separate follow-up PRs.
