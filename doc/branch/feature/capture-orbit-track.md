# Deterministic Capture Orbit Track

## Scope

Second medium-complexity step after
[the update/clock boundary](capture-update-clock-boundary.md). This is a minimal
CLI validation track, not a general animation editor or camera snapshot system.

Use `-CaptureSessionOrbitDegreesPerSecond 90` together with
`-CaptureSessionClock fixed-step` and the existing Capture Session options.
Signed rates in [-360, 360] are accepted; zero produces a stationary track.
Real-time clock and competing reflection/single-capture/history-validation
automation are rejected explicitly.

## Pose Contract

- Capture the starting Arcball baseline around the saved camera target: yaw,
  pitch, distance, and pivot. The track does not animate projection settings;
  interactive projection editing during CLI validation is outside this contract.
- Output frame `n` is evaluated at `n/FPS`, independently of wall time,
  physical render count, previous evaluations, or encoder completion timing.
- Yaw is baseline yaw + angular rate times logical time. Reduce it periodically
  before converting to float; use the existing DebugCameraController to apply
  the pose. Its existing pitch-limit/world-up conventions remain in effect.
- Delay and Warmup hold frame-zero camera pose. They do not move the track ahead.
- Apply the camera after the sample scene update and before publishing the scene
  and scheduling its capture, so scene camera animation cannot overwrite it.
- Paused/capacity-wait/drain frames do not advance the track. The last accepted
  camera remains in place while output drains.
- While the CLI track is active, camera mouse/keyboard controls are ignored;
  UI and ROI handling retain their existing paths. Normal captures without the
  flag are unchanged. Manual camera blending is out of scope.
- `[CAPTURE_CAMERA]` reports output index, logical time, orbit parameters and
  actual published camera position. It is not GPU camera-buffer readback proof.

## Verification

- Unit tests cover baseline preservation, reverse/static orbit, repeated and
  out-of-order evaluation, logical timing, controller-applied position, and
  invalid FPS. CLI tests cover signed rates and invalid/conflicting settings.
- GPU validation uses an asymmetric lit cuboid fixture generated under `bin`,
  rather than the uniform input-plane used by format-only tests.
- PNG/MP4 orbit cases check one pose per accepted output, exact `n/FPS`, expected
  angular phase and camera position, and first/last image variation.
- MP4 inspection checks all frames, strict PTS/duration, and first/last decoded
  luma variation. A PT orbit case is a runtime motion smoke test, not denoising,
  convergence, history-reset or ghosting quality acceptance.

## Verified Results

- CMake Debug and SDK-present MSBuild Debug x64 succeeded; CTest passed 28/28.
- Final SDK-present GPU run passed four cases: regular MP4 ROI, Orbit PNG,
  Orbit MP4, and Orbit PT MP4.
- All Orbit cases retained 12 frames at 30 FPS. Pose/time logs matched every
  output index and expected angular phase across presentation-only wait frames.
- The non-default starting position `(1, 0, -5)` was preserved within float
  precision; no default Arcball view replaced the initial camera.
- First/last PNGs were inspected visually and showed changing cuboid projection.
  Both moving MP4s passed decoded luma-change, full frame count and strict PTS/
  duration checks. This is motion evidence, not PT convergence/ghosting proof.
- MP4 pools used two allocations and ten reuses for 12 frames, stayed within
  count/byte limits, and ended with zero pending GPU/encoder work.
- No captured D3D12 ERROR/CORRUPTION or capture save error was reported.

Evidence: `bin/CapturePort/capture-orbit-final-20261014/report.json`. Its base
commit field is `3113f15`; hashes identify the tested working-tree binaries.
Generated scenes, images, videos and logs are not committed.

The initial `capture-orbit-20261014` run checked motion but did not assert the
starting view. Review found that `SetObjectViewerState` immediately changes the
camera. The new pivot-aware `InitObjectViewerFromCamera` overload avoids that
side effect; unit and final runtime tests explicitly check starting-position
preservation. The final fixture also specifies its camera in the render preset,
which otherwise applies camera defaults independently of the scene document.

## Remaining High-Complexity Work

Manual/live-preview versus capture-camera ownership, detailed pause/step and
editing policy, controlled encoder-delay injection, longer moving-camera PT
validation with reset/sample metadata and reference comparisons, and any
samples-per-output-frame convergence policy remain separate work.
