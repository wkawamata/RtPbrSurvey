# Capture Update and Clock Boundary

## Scope

First medium-complexity follow-up to the
[MP4 readback pool](mp4-capture-readback-pool.md). This separates update decisions,
camera/editor interaction, scene simulation, and capture scheduling without
introducing scene snapshots or changing the public renderer frame API.

## Frame Flow

1. Poll capture completions; this cannot advance a clock or generate a pose.
2. Process the existing UI and pending host actions.
3. Choose an explicit `CaptureFrameUpdateMode`.
4. On an advancing frame, update the host clock, apply existing camera/editor
   interaction, update the scene, then publish capture scheduling timing.
5. On a paused/waiting/draining frame, poll scheduling with a frozen update
   index and simulation time, then present without advancing renderer simulation.

`UpdateSceneInteraction` and `UpdateSceneSimulation` preserve the previous
ordering. Keyboard movement is still applied only on an advancing scene update.
Win32 mouse/key event handling and ImGui processing remain in their existing
ownership boundaries. This is not separate live-preview/capture camera state.

## Clock Policy

- Live scene: use the existing wall-frame delta for simulation and interaction.
- Fixed-step Warmup/Recording: use `1/FPS` for scene updates and compute
  simulation time as integer fixed-update count divided by FPS.
- Fixed-step capacity wait and Pause: do not advance scene update count or time.
- Fixed-step Draining/Finalizing: completion-only frames cannot advance the pose
  even when encoder capacity has become available.
- Start delay: wall-time scheduling continues; unpaused scene updates stay live
  and do not consume fixed capture ticks.
- Real-time output pressure: live updates remain independent of encoder capacity.
- After completion/failure: return to normal live updates, respecting Pause.

Warmup uses the qualifying scene-update index supplied in
`CaptureSessionTiming::renderFrameIndex`, not presentation-only frame count.
The standalone app retains its separate physical render counter for logging and
existing single-screenshot/reflection automation.

`CaptureSession::CanAdvanceFixedStep` also rejects advancement after the accepted
frame/duration limit and during fixed-step drain/finalize. This prevents an extra
scene update solely to discover that output generation has already ended.

Fixed-update count includes Warmup, but excludes start delay. It is not the
output-frame index. Future camera tracks should use output logical time
`acceptedFrameCount/FPS`, with an explicit recording-start baseline.

## Tests

- 1800 logical updates retain exactly `n/FPS` without accumulated clock drift.
- Scheduler cadence is also checked for 1800 outputs at 24/30/60/120/240 FPS,
  using both frame and duration limits. Fixed reservation times derive from
  recording start + accepted frame count/FPS. Comparisons permit only a few
  double-precision ULPs, avoiding a one-update skip at an otherwise identical tick.
- Pause, capacity wait and drain do not consume an update or change fixed time.
- Start delay remains wall-time driven, independent of encoder capacity.
- Real-time live simulation and wall recording time remain distinct.
- A delayed session cannot finish Warmup through 100 presentation-only polls.
- Two actual scene updates finish a two-update Warmup, and capacity recovery
  resumes at the next fixed tick.
- An accepted output limit blocks an extra pose even with a free pipeline slot.

The multi-FPS matrix is CPU clock/scheduler validation, not moving-camera proof.

## Verified Results

- CMake Debug and SDK-present MSBuild Debug x64 succeeded.
- CTest passed 28/28. The clock/session suite also passed ten repeated runs.
- Eight SDK-present GPU cases passed: PNG ROI, EXR sequence, GIF repeat,
  odd-size MP4 ROI, full-HD MP4, real-time MP4, PT MP4, and a long fixed ROI MP4.
- The long ROI case retained and decoded all 1800 frames at 60 FPS, with strict
  consecutive PTS and a separately checked 30-second media duration.
- Its simulation timing advanced exactly one `1/60` interval per accepted
  frame despite presentation-only wait frames. Frame 1799 was accepted on
  physical render 2704, demonstrating that physical presentation count is not
  used as the capture simulation clock.
- The long case used two readback allocations and 1798 reuses; peak reserved
  RGBA8 bytes and allocated readback bytes were each 32,768. Final GPU pending
  count and encoder reserved/owned count/bytes were all zero.
- Fixed short runs retained all expected frames. Real-time duration validation
  passed for 1.5 seconds; its retained frame count is not a sustained FPS claim.
- No captured D3D12 ERROR/CORRUPTION or capture save error was reported.

Final evidence: `bin/CapturePort/capture-update-clock-final-20261014/report.json`.
Its base commit field is `3113f15`; hashes identify the tested working-tree
executable and inspector. Generated artifacts remain uncommitted.

The earlier `capture-update-clock-20261014` run covered short regressions before
the long CPU matrix detected the rounding mismatch. The final run includes
that fix. All GPU cases use a static scene; no encoder-delay injection or
moving-camera quality claim is made here.

## Next Medium Step

The minimal CLI Orbit implementation is recorded in
[the deterministic Orbit track](capture-orbit-track.md).

Add a minimal deterministic camera track evaluated directly at output logical
time. Preserve existing reflection automation, and do not add general scene
snapshot infrastructure. Concurrent manual/live camera ownership, detailed
pause/step interaction policy, and moving-camera PT evidence remain separate
high-complexity work.
