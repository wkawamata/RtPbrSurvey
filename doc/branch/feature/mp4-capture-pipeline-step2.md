# MP4 Capture Pipeline: Bounded GPU and Encoder Overlap

## Scope

Follow-up to [the encoder worker](mp4-encoder-worker-validation.md), on
`codex/mp4-encoder-worker`. This step supports multiple session-owned requests
and overlaps one GPU readback with prior MP4 encoding. The GPU readback resource
is still allocated per capture; a reusable multi-slot pool remains future work.

## Contracts

- `CaptureSession` tracks all outstanding request IDs. Its default concurrency
  remains one; `SceneRenderer` selects two for MP4.
- GPU-complete MP4 requests detach from the screenshot submission slot while
  retaining asynchronous output ownership. Encoder results complete detached
  requests by ID and cannot release a newer GPU request.
- Legacy screenshot consumers reserve every session-owned request ID.
- The app polls completions before deciding whether to advance a fixed-step
  frame. Polling does not generate another capture request or change its clock.
- Fixed-step generation checks request count, GPU submission availability, and
  worker count/byte capacity before advancing the pose. It can advance while
  the previous frame is encoding, but waits when capacity is exhausted.
- Real-time rendering continues under output pressure. Missed intervals are
  counted, and PTS preserve elapsed recording time. Late intervals are skipped
  rather than queued with stale camera poses.
- Stop and failure retain ownership until every accepted request completes.
  The first error survives later cancellation errors. MP4 finalization begins
  only after all requests drain.
- MP4 dimensions/ROI are checked against the worker's byte budget at start.
  Output dimensions are fixed per session; a resize is reported as a capture
  failure instead of leaving capacity polling stuck indefinitely.

Capacity prediction is safe in the current single-producer, single-readback
path: only the render thread adds work, and the worker can only release capacity
between admission and GPU completion. A multi-readback pool will require explicit
reservations for every GPU slot's future CPU buffer.

## Verification

- CMake Debug app/tests and SDK-enabled standalone MSBuild Debug x64 succeeded.
- Full CTest suite passed 28/28, including concurrent requests, out-of-order and
  duplicate results, legacy result isolation, complete drain on failure, fixed
  backpressure, and real-time timestamp gaps.
- SDK-enabled GPU capture passed seven cases: PNG ROI, EXR ROI, GIF repeat,
  odd-size MP4 ROI, full-HD MP4, real-time MP4, and PathTracing MP4.
- Every case exited successfully. Output decoding, counts, fixed-step timestamp
  intervals, and real-time duration checks passed. No D3D12 ERROR/CORRUPTION or
  capture save error was recorded in these runs.
- Full-HD MP4 decoded all three frames at the 30 FPS output cadence.
- The full-HD log demonstrates overlap:

```text
[CAPTURE_PIPELINE] accepted=2 saved=0 encoder_pending=1 encoder_bytes=8294400
[CAPTURE_PIPELINE] accepted=3 saved=1 encoder_pending=1 encoder_bytes=8294400
```

The next simulation step and capture request were accepted while the
previous frame was still owned by the encoder. The same video retained the
expected frame count and PTS.

The 1.5-second real-time run retained 44 frames and preserved elapsed duration.
This is not sustained 60 FPS proof; the test uses a static scene and a small ROI.

Generated evidence: `bin/CapturePort/mp4-pipeline-step2-20261014/report.json`.
Its commit field identifies base `66bf912`; executable/inspector hashes identify
the tested working-tree binaries. Artifacts remain uncommitted under `bin`.

## Remaining Work

- Replace per-capture readback allocation with bounded reusable GPU slots and
  explicit downstream memory reservations.
- Separate live camera input/preview from deterministic capture simulation and
  add a camera track evaluated at logical frame time.
- Move or optimize render-thread pixel conversion after measuring its cost.
- Add moving-camera PT validation and 1800-frame pressure tests.

Fixed-step generation intentionally waits at full capacity to preserve all
logical frames. This step removes waiting for each individual encode; it does
not promise indefinitely advancing fixed-step time with finite memory.
