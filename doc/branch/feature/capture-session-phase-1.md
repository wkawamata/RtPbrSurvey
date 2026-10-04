# Capture Session Phase 1

Phase 1 adds a host-public, GPU-wait-free scheduler in `Runtime/CaptureSession.h` and a thin `SceneRenderer` bridge. It is intentionally limited to image sequences:

- PNG captures the final composed output, including ImGui.
- EXR captures linear pre-tone-map scene color, excluding ImGui.
- GIF and MP4 are recognized output formats but fail `Start` explicitly until their encoder phases land.

## Host contract

The host supplies render-frame, real-time, and simulation-time values through `CaptureSessionTiming`. `Update` may expose one `ScreenshotRequest`; the host acquires it, submits it, calls `MarkRequestAccepted`, and reports the asynchronous `ScreenshotResult` through `CompleteRequest`.

Only one session request is in flight. This deliberately matches the renderer readback path and prevents an unbounded stale capture queue. Each session request receives an opaque token; its result is consumed by that token, so legacy screenshot results cannot complete a session and legacy consumers cannot take a session result.

- Real-time sessions advance their deadline while busy and increment `droppedFrameCount` for each missed deadline.
- Fixed-step sessions never drop a required frame. `CanAdvanceFixedStep` is false while a request is ready and while its renderer readback is in flight; no GPU wait is issued.
- `Stop` accepts no new request, drains the in-flight result, then reaches `Completed`. A failed output reaches `Failed` after the same cleanup boundary.
- Duration begins when recording starts, after warmup. Warmup time is deliberately excluded so a requested recording duration is stable when warmup changes.

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
-CaptureSessionRoi <x> <y> <width> <height>
-CaptureSessionDurationSeconds <seconds>
-ExitAfterCapture
```

`-CaptureSessionOutputDir`, `-CaptureSessionBaseName`, and either `-CaptureSessionFrames` or `-CaptureSessionDurationSeconds` are required. Capture Session is mutually exclusive with `-CapturePath` and Reflection capture plans. Session request tokens keep legacy and session results separate, and a session refuses to start while a legacy request is pending.

The reusable ImGui panel, GIF encoder, and MP4 encoder remain separate follow-up PRs.
