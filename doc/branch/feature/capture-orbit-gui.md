# Capture Orbit GUI

## Scope

Expose the existing deterministic Orbit track in the standalone application's
Capture Session panel. This is one camera and one render stream, not independent
preview/capture cameras or a general camera animation editor.

The existing scene updates already receive a fixed 1/FPS delta during eligible
fixed-step capture updates. No general scene camera-animation API currently
exists. Orbit is the first GUI-selectable camera track for deterministic output.

## Contract

- Orbit camera is off by default. Enabling it selects the fixed-step clock.
- Speed is degrees/second in [-360, 360]; negative reverses direction and zero
  holds the camera. Invalid or real-time Orbit settings block Start, including F8.
- Each Start snapshots the current camera position/target into an immutable
  Orbit baseline. Frame n uses n/FPS, regardless of encoder/readback wall time.
- Delay and warmup hold pose zero. Capacity waits, pause and drain hold the last
  accepted pose. Settings cannot change during an active session.
- Camera mouse/keyboard input and the standalone Camera panel are disabled while
  the Orbit owns the pose, including draining. They return after completion or
  failure. The ending pose remains; there is no automatic return to the start.
- P controls frame advancement and F requests one update while paused. Space
  only pauses the scene's own animation; Orbit follows output-frame time.
- CLI and GUI starts pass their Orbit settings explicitly. A prior CLI Orbit
  does not implicitly enable a later GUI session.
- Track settings live in App, not the host-neutral CaptureSessionConfig or SDK
  layer. Existing captures without Orbit retain manual camera behavior.

## Paused-Start Fix

Native GUI verification found that starting while P-paused could accept output
zero from a frozen render. SceneRenderer::UpdateCaptureSession now accepts an
optional allowFrameSubmission flag (default true for existing host calls).
The standalone app passes false on pause/capacity-wait/drain frames. Timing and
completion can progress without scheduling or consuming a ready screenshot.

A fixed-step session anchors its recording start to the first eligible output
update, so polling before output zero cannot shorten a duration-limited movie.
Tests cover paused starts with zero warmup, with and without start delay, and
require all three outputs for a three-frame duration after resuming.

## Verification

- Final CMake Debug and SDK-present MSBuild Debug x64 succeeded; CTest passed
  28/28, including paused-start duration/delay regression tests.
- Native GUI validation used the Windows sky API and DamagedHelmet. Orbit
  enabled the fixed clock, and speed/settings were locked during the session.
- Starting with P-paused retained zero accepted/saved outputs. F submitted
  exactly one output, after which the count stayed at one until P resumed.
- Resume completed all 60 outputs with zero reported drops. MP4 decoding verified
  1920x1080 H.264, 60 frames at 60 FPS, strict timestamps/duration, and first/last
  luma variation. The viewport visibly moved around the model.
- Completion released capture capacity; UI reported two readback allocations
  and 58 reuses. Viewport wheel zoom worked again after completion.
- Final CLI GPU regression passed normal ROI, normal PT Orbit, delayed moving
  PT and delayed stationary PT. Both delayed PT cases retained 12 outputs at
  30 FPS, camera/reset/sample semantics, and zero pending work after drain.
- Delayed moving PT exercised 53 capacity waits and 12 drain frames; stationary
  PT exercised 52 waits and 11 drain frames. No captured CLI D3D12 ERROR/
  CORRUPTION or capture save error was reported. These CLI results are separate
  from the GUI observation; the GUI launch did not enable debug-file logging.

GUI media: `bin/CapturePort/gui-orbit-final-output-20261014/capture.mp4`.
CLI evidence: `bin/CapturePort/gui-orbit-regression-20261014/report.json`.
Generated captures and logs are not committed.

The first GUI run exposed the paused-start bug and is not the final acceptance
run. A relink attempt also failed while its renderer was still open; after
closing that instance, MSBuild and the corrected runtime validation succeeded.

## Remaining Work

Long deterministic motion under pressure, interactive real-time latency/drop
measurements, per-output PT convergence, and richer scene/camera tracks remain
separate work. No simultaneous independent live-preview camera is implemented.
