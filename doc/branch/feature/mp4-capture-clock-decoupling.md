# MP4 Capture Clock Decoupling Plan

Status: reviewed design; dedicated encoder worker and asynchronous finalization
implemented. See [implementation and validation](mp4-encoder-worker-validation.md).
Multiple-request tracking and bounded GPU/encoder overlap are also implemented;
see [pipeline step 2](mp4-capture-pipeline-step2.md). Full clock separation and
multiple reusable readback slots remain pending.

## Goal and Limits

Keep interactive camera movement responsive during MP4 recording and preserve
the intended video timeline when GPU readback or encoding is slow.

A finite queue absorbs temporary delays. It cannot guarantee real-time,
lossless recording when sustained rendering or encoding throughput is below
the requested FPS. The capture contract must make this tradeoff explicit.

## Review Findings

- The previous plan advanced fixed-step camera time unconditionally while
  promising bounded memory and no dropped frames. These promises conflict
  under sustained overload.
- A scene snapshot does not remove synchronous encoder work from the render
  thread. Moving that work is the first implementation priority.
- Fixed-step PTS already use frame index in `Mp4Encoder::AppendFrame`.
  Preserve this behavior and carry explicit sequence metadata through queues.
- `WriteSample` success means the sink writer accepted a sample, not that the
  file is finalized. Completion counters must describe observable milestones.
- A general `SceneState` snapshot is broader than the first fix requires.
  Capture owned pixel buffers first; introduce scene snapshots only when a
  separate capture render timeline becomes necessary.

## Current Implementation Evidence

- `App/RtPbrSurveyApp.cpp`: `OnIdle` derives `advanceFrame` from pause state and
  `CanAdvanceCaptureSessionFixedStep()`.
- `Engine/RtPbrSurveyEngine.cpp`: `UpdateFrame(false)` skips the sample update
  callback and camera constant-buffer advancement. PathTracing dispatch also
  follows frame advancement.
- `Runtime/CaptureSession.cpp`: the scheduler tracks one ready request and one
  in-flight request. Fixed-step generation waits for that request to complete;
  real-time scheduling counts missed capture intervals.
- `Engine/RtPbrSurveyEngine.cpp`: `ProcessCompletedScreenshot` checks a
  GPU fence, maps readback data, converts pixels, and calls `AppendFrame` on the
  frame thread. This call can stall live rendering even in real-time mode.
- `Renderer/Mp4Encoder.h`: all encoder calls and destruction belong to one
  thread. Initialization, append, finalize, abort, and destruction must move
  together to the worker.
- `Renderer/Mp4Encoder.cpp`: default timestamps are
  `frameIndex * 10000000 / FPS`; explicit timestamps support real-time capture.

These observations identify mechanisms to test. They do not establish whether
the user's specific stall was caused by encoding, GPU cost, pause state, or a
combination. Add timing evidence before attributing all stalls to one stage.

## Recording Contracts

### Real-time / interactive recording

- Camera input and scene animation follow elapsed wall time.
- Encoder availability does not gate live simulation or rendering.
- Capture samples the most recently rendered frame at the requested cadence.
- If no capture slot is available, skip that capture interval and count it.
- Preserve elapsed recording time in PTS. Do not renumber retained frames into
  a shorter timeline after dropping frames.
- The initial overload policy is skip-new capture requests. Frames already
  queued retain their order; UI reports dropped intervals and queue pressure.

This mode is the default recommendation for manually operated camera recording.
GPU rendering below target FPS still produces visible holds or dropped samples.
Encoder decoupling cannot create camera views that were never rendered.

### Fixed-step / deterministic recording

- Generate logical frame `n` at simulation time `n / FPS`.
- Capture pose, output sequence, and PTS must refer to the same logical frame.
- Reserve downstream capacity before advancing the capture simulation.
- If capacity is exhausted, pause generation and drain queued work. Wall-clock
  recording may take longer, while video motion and duration remain correct.
- Do not drop logical frames in this mode.
- Keep UI and input event processing active during backpressure. Interactive
  input is applied to the next generated frame and is not deterministic.

For smooth repeatable camera animation, evaluate a camera track at `n / FPS`.
If responsive live preview and independent offline capture are both required,
they need separate camera/render state. That is a later extension, not part of
the initial bounded-queue implementation.

## Initial Pipeline and Ownership

```text
App: input, simulation, capture scheduling
    -> Render thread: render + copy ROI into a readback slot
    -> Fence completion: copy mapped pixels to an owned CPU buffer
    -> Bounded encoder queue
    -> Worker: MP4 append / finalize / abort
    -> Completion mailbox consumed by the render thread
```

The worker never accesses mapped D3D12 memory, live scene state, ImGui, or
renderer-owned mutable resources. Each queued frame owns its pixel bytes and
contains session ID, request ID, logical sequence, dimensions, PTS, and format
metadata. Session configuration is immutable while active.

Use a bounded pool of readback slots. Reuse a slot only after its fence completes
and its pixels have been copied. Enqueue CPU frames in submission order even if
completion polling observes several ready slots at once.

Bound memory by bytes as well as frame count. Eight 3840x2160 RGBA8 frames alone
require about 253 MiB; readback allocations and conversion buffers add more.
Choose initial capacities from measured ROI sizes and memory budgets instead
of hard-coding eight frames for every resolution.

## Timeline and Result Contract

- Fixed-step PTS: integer arithmetic `n * 10000000 / FPS`; duration is the
  difference between adjacent frame timestamps to avoid accumulated rounding.
- Real-time PTS: actual rendered-frame capture time relative to recording start,
  measured by a monotonic clock. Preserve gaps and strict monotonicity.
- End timestamp belongs to the recording stop/limit time, not finalization time.
- Start delay uses wall time. Warmup counts actual qualifying render updates,
  not idle polls or encoder completions. Recording begins after both finish.
- Delaying and Warmup do not enqueue output frames.
- Stop stops new scheduling, then drains accepted GPU copies and CPU frames.
- Completed is published only after worker finalization succeeds.
- Worker failure transitions the session to Failed, releases outstanding slots,
  and preserves the existing incomplete-output cleanup policy.
- Scene close, application shutdown, and resize must honor draining. Freeze
  capture dimensions/ROI per session; stop and drain before an incompatible
  resize. Join the worker before destroying device-dependent capture resources.

Keep existing `acceptedFrameCount` and `savedFrameCount` compatibility explicit.
Add counters for scheduled intervals, GPU submissions, readbacks ready, writer
accepted frames, dropped intervals, queue depth/bytes, and finalization state.
Do not expose an `encodedFrameCount` unless the backend can actually report it.

## PathTracing Contract

Every rendered capture frame uses the corresponding camera constant buffer and
scene pose. Camera changes invalidate accumulation before tracing that pose.
Polling a fence or draining an encoder must not advance PT sample indices.

Initially moving-camera MP4 records the samples available from each render
frame. A configurable samples-per-output-frame convergence policy requires a
separate design and is outside this work.

## Implementation Sequence

1. Add baseline measurements for frame time, readback conversion, `AppendFrame`,
   finalize, capture backpressure, camera pose, sequence, and PTS. Add controlled
   encoder-delay injection for tests.
2. Add a bounded MP4 worker queue with owned pixel buffers and a completion
   mailbox. Move all encoder lifetime operations to that worker. Keep the
   existing single readback slot initially to isolate the threading change.
3. Change session completion tracking to support multiple outstanding request
   IDs. Separate GPU acceptance, writer acceptance, draining, and finalization.
   Update screenshot-result routing to reserve every session-owned request.
4. Add a bounded readback-slot pool and downstream capacity reservation. Route
   real-time overload to counted capture skips and fixed-step overload to
   capture-generation backpressure.
5. Extract input polling, live simulation, deterministic capture simulation,
   rendering, and completion polling at their existing ownership boundaries.
   Keep public APIs focused; do not replace `advanceFrame` with unexplained
   booleans or introduce a generic scene snapshot API prematurely.
6. Expose queue/cadence diagnostics and document which clock to use for manual
   recording versus deterministic animation. Add camera-track validation.

Primary modules: `App/RtPbrSurveyApp.*`, `Runtime/CaptureSession.*`,
`Runtime/SceneRenderer.*`, `Engine/RtPbrSurveyEngine.*`, and
`Renderer/Mp4Encoder.*`. Keep the initial worker path MP4-specific and verify
existing screenshot formats against their current behavior.

## Verification and Acceptance

- Scheduler tests with artificial clocks: delay, warmup, limits, pause/step,
  fixed-step capacity reservation, real-time skips, and timestamp gaps.
- Worker tests with an injected slow/failing writer: bounded memory, ordering,
  stop/drain, failure, finalize exactly once, and shutdown with pending work.
- GPU integration: fence-safe slot reuse, ROI, odd-size padding, resize,
  scene close, session restart, and D3D12 Debug Layer checks.
- PT integration: camera movement resets history; paused/draining frames do not
  accidentally advance sample indices; captured pose matches frame metadata.
- Record a 1800-frame, 60 FPS deterministic camera track under encoder delay.
  Require 1800 retained frames, 30-second media duration, continuous fixed-step
  PTS, bounded memory, and correct camera poses. Longer wall time is acceptable.
- Record interactive real-time camera motion under encoder delay and GPU load.
  Require live simulation to stay independent of encoder queue capacity, media
  duration to follow elapsed recording time, and all missed intervals to be
  counted. Compare UI/frame latency against the measured baseline.
- Inspect actual MP4 timestamps, frame count, duration, and decoded camera
  motion. File existence alone is insufficient.
- Run PNG/GIF/EXR, single screenshot, and non-capture regression checks.

## Deferred Extensions

Separate live-preview and capture scene snapshots, camera input recording and
replay, alternate drop policies, hardware encoder selection, GPU color
conversion, and zero-copy encoding should follow measured bottlenecks and
specific product requirements.
