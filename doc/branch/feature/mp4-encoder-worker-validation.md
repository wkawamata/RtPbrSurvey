# MP4 Encoder Worker Implementation and Validation

## Scope

This document records the initial worker step. For the current multiple-request
behavior and newer GPU evidence, see [pipeline step 2](mp4-capture-pipeline-step2.md).

Branch: `codex/mp4-encoder-worker`.

Implements the dedicated encoder thread and asynchronous MP4 finalization from
[the clock decoupling plan](mp4-capture-clock-decoupling.md). The existing single
GPU readback and single in-flight capture request remain in use. Full camera
clock separation and multiple readback slots are subsequent work.

## Implementation

- `Renderer/Mp4EncoderWorker.*` owns a lazily started worker thread. Writer
  creation, COM/Media Foundation initialization, NV12 conversion, append,
  finalize, reset, and destruction all happen on that thread.
- Render-thread fence polling maps completed readback, converts to an owned
  RGBA8 buffer, unmaps it, and moves it to the worker. The worker has no GPU
  resource or mapped-memory access.
- Admission is bounded by frame count and owned byte count. Defaults are two
  frames and 128 MiB, including the executing frame. Unconsumed results also
  count against frame admission. The current scheduler admits only one frame
  at a time, so the queue is preparation for later pipeline work.
- A completion mailbox returns each request's identity and append result to
  the render thread. The screenshot queue remains pending until that result.
- MP4 session transitions are `Recording -> Draining -> Finalizing -> Completed`.
  Finalizing remains active and blocks restart and pending host actions until
  the writer completes. Append/finalize failures transition to Failed.
- Recording end time is frozen on leaving Recording, so drain/finalize latency
  cannot lengthen a real-time movie. Existing fixed-step rational PTS are kept.
- Abort cancels queued frames; shutdown joins the worker before destruction.
  An in-progress backend call must return before shutdown can complete.
- UI diagnostics show outstanding buffers/bytes and append/finalize time.
  Append success retains the existing saved-frame counter semantics; the file
  is usable only after finalization succeeds.

## Automated Tests

- Controlled writer gates prove enqueue and status polling remain available
  while append or finalize is stalled.
- Tests cover request order/identity, independent byte and frame limits,
  repeated finalize requests, blocked restart, empty sessions, append/finalize
  failures and exceptions, abort, shutdown with pending work, and restart after
  failure. Backend lifetime and every call use the same worker thread.
- The worker test passed ten consecutive executions after the final changes.
- A real worker-generated odd-size MP4 is fully decoded, checking H.264,
  padded dimensions, frame count, timestamps, and duration.
- The full CTest suite passed 28/28. The capture-specific suite passed again
  after the final worker changes.
- CMake Debug app/tests and standalone SDK-enabled MSBuild Debug x64 succeeded.
  MSBuild uses the existing vcpkg installation under
  `out/build/sdk-free-exr/vcpkg_installed`; set `VcpkgRoot` and
  `VcpkgInstalledDir` explicitly when the shell does not resolve dependencies.

## GPU Validation

SDK-free Debug application, all cases exited successfully with no captured
D3D12 ERROR/CORRUPTION or save errors:

| Case | Result |
| --- | --- |
| MP4 odd ROI, 60 FPS | 63x47 padded to 64x48; all 5 frames and timeline decoded |
| MP4 ROI, 30 FPS | 64x64; all 5 frames and timeline decoded |
| MP4 real-time, 1.5 seconds | 23 accepted frames; elapsed duration preserved |
| MP4 PathTracing | 64x64; all 3 frames and 30 FPS timeline decoded |
| PNG ROI | All 3 images decoded at 64x64 |
| EXR ROI | All 3 images decoded with finite channel values |
| GIF repeat | All 3 frames decoded; repeat metadata preserved |

Artifacts (generated, not committed):

- `bin/CapturePort/mp4-worker-sdk-free-mp4-20261014/report.json`
- `bin/CapturePort/mp4-worker-other-formats-20261014/report.json`

The SDK-enabled standalone Debug application also passed three GPU cases:
1920x1080 MP4 (all 3 frames at 30 FPS), 1.5-second real-time MP4 (22 accepted
frames with elapsed duration preserved), and PathTracing MP4 (all 3 frames at
30 FPS). Each run exited successfully with no captured D3D12 ERROR/CORRUPTION
or save errors. Details:
`bin/CapturePort/mp4-worker-sdk-present-20261014/report.json`.

The reports' `testedCommit` is the branch base, `66bf912`; the implementation
was tested as working-tree changes. Executable/inspector hashes in each report
identify the actual tested binaries.

Reproduce the SDK-enabled GPU checks with a fresh output directory:

```powershell
python -B Tests/CaptureSession/validate_standalone_formats.py --output bin/CapturePort/mp4-worker-next-run --cases mp4-full,mp4-real-time,mp4-path-tracing --inspector out/build/sdk-free-exr/Debug/RtPbrSurvey.Mp4EncoderTests.exe --executable bin/x64/Debug/RtPbrSurvey.exe
```

Initial regression attempts stopped in Python inspection because Pillow, NumPy,
and OpenEXR were absent. These packages were installed under the ignored
`bin/CapturePort/python-packages` directory before successful validation.

## Remaining Work

- Fixed-step capture still waits for its one in-flight request to complete.
  Its camera/simulation timeline has not yet been separated from backpressure.
- GPU copy/pixel conversion remains on the render thread. GPU load can still
  limit rendering, and real-time capture can still skip intervals.
- The short real-time test is a timeline validation, not sustained 60 FPS proof.
- The test scene is static. Smooth camera animation, moving-camera PT quality,
  user-operated preview responsiveness, and 1800-frame stress validation belong
  to the later clock/readback phases.
- Memory limits cover worker-owned RGBA buffers, not opaque Media Foundation
  internal buffers or GPU allocations.
