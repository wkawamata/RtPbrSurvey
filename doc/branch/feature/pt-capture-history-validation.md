# PT Capture History and Backpressure Validation

## Scope

Follow-up to committed capture clocks/Orbit (`5b5e8d2`). This adds observable
PT submission state for each physical capture frame and reproducible encoder
pressure. It does not change PT shaders, history policy, or camera ownership.

## Diagnostics

`[CAPTURE_PT]` is written after `RunFrame` when capture is active and PT is
selected. It includes physical render index, accepted output index (or -1),
explicit update mode, advancement flag, pre/post accumulated samples and sample
index, history validity, last reset reason, samples/update, and the renderer's
CPU camera position.

The before state is taken before the host scene update, not immediately before
the PT dispatch. A camera change may reset it during that update. The after
state reflects committed command submission, not a GPU counter readback.
Output pixels are separately validated after fence completion and MP4 decoding.

## Controlled Pressure

`-CaptureSessionEncoderDelayMs 100` injects an extra 100ms before each MP4
append. Accepted range is [0, 1000]; zero is the default. Nonzero delay requires
MP4. This is a diagnostic option, not a playback-speed control.

The wait runs only on the encoder thread, retains normal count/byte accounting,
is immutable while a session is active, and is interrupted by abort/shutdown.
Append timing includes the injected delay. It does not emulate arbitrary GPU
latency or Media Foundation hangs, nor make a blocked native backend cancellable.

## Assertions

- A moving Orbit pose after output zero must report a Camera reset, exactly
  one batch of samples, and a restarted sample index.
- A stationary Orbit must add exactly samples/update on every advancing frame.
- Presentation-only capacity waits and fixed-step drain frames must preserve
  accumulated sample count and sample index.
- Each output has exactly one matching post-render diagnostic and CPU camera
  position corresponding to its logged logical pose.
- Artificial pressure must actually exercise capacity wait and drain frames.
- Counts, PTS, duration, readback reuse and memory bounds retain existing checks.

Fixtures use 12 outputs at 30 FPS, four samples/update, and a 100ms append delay.
The moving case also checks decoded image variation. The stationary case does
not require pixel identity because stochastic PT samples legitimately change.

## Verified Results

- CMake Debug and SDK-present MSBuild Debug x64 succeeded; CTest passed 28/28.
- The worker tests also passed ten consecutive runs, including delayed append
  cancellation and capacity release.
- All four GPU cases passed: normal MP4 ROI, normal moving PT Orbit, delayed
  moving PT Orbit, and delayed stationary PT Orbit.
- Both delayed cases retained 12 decoded outputs at 30 FPS, with four samples
  per advancing update. Each logged 23 physical frames, including six capacity
  waits and two drain frames. Wait/drain frames preserved samples, sample index,
  history validity, and CPU camera position across frame boundaries.
- Moving outputs after frame zero reset Camera history and submitted one new
  sample batch; stationary updates accumulated four additional samples.
- Measured maximum append times were 176.0335ms (moving) and 174.2002ms
  (stationary), including the configured 100ms diagnostic wait.
- Output identities, logical poses, decoded frame counts, strict PTS/duration,
  and readback budgets passed. The moving case passed decoded luma variation.
- Each 12-frame pool used two readback allocations and ten reuses, and ended
  with zero outstanding GPU/encoder work. No captured D3D12 ERROR/CORRUPTION or
  capture save error was reported.

Evidence: `bin/CapturePort/pt-capture-history-final-20261014/report.json`.
The report records base commit `5b5e8d2` and working-tree binary hashes.
Generated fixtures, logs and videos are not committed.

The subsequent [GUI Orbit and paused-start fix](capture-orbit-gui.md) reran all
four cases successfully. Its evidence is
`bin/CapturePort/gui-orbit-regression-20261014/report.json`; wait/drain counts
differ with submission gating and runtime throughput, while output identities,
camera/sample invariants and media timing remain the acceptance criteria.

## Remaining Work

GPU camera/sample-index readback evidence, longer moving-camera reference
comparisons, residual/ghosting measurements, real-time/manual camera ownership,
and input/pause/step policy remain separate work. Passing these state assertions
is not a full temporal image-quality acceptance.
