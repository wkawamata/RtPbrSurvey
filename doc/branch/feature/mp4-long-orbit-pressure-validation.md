# Long Orbit Under Encoder Pressure

## Setup

- Commit: `3eb8cc013a70e7d5ca202294de77638a07b97763`
- Output: `bin/CapturePort/mp4-orbit-long-pressure-20261014-run1`
- Renderer: SDK-present Debug x64 build
- Scene: asymmetric cube with a nonuniform scale, viewed by the deterministic
  capture Orbit
- Capture: 640x480 ROI, 60 FPS, 1,800 frames, 37 degrees/second, 100ms
  diagnostic delay before each encoder append
- Runtime: approximately three minutes; encoder backpressure extended wall time
  while fixed-step video time remained unchanged

## Results

- The process exited successfully and accepted/saved all 1,800 output frames.
- The decoded H.264 MP4 is 640x480 at 60 FPS with exactly 1,800 frames and a
  30-second duration. The inspector checked strictly increasing timestamps,
  each frame's expected fixed-step presentation time, and positive sample
  durations. First-to-last decoded luma differed, confirming camera motion.
- All 1,800 camera samples matched the deterministic Orbit formula and frame
  index. No pose was skipped or repeated.
- Encoder append delay was configured to 100ms; maximum measured append time
  was 174.4813ms.
- Encoder reservations peaked at 2 frames and 2,457,600 bytes, below the
  2-frame / 128 MiB limits. GPU readback pending peaked at 1 of 2 slots.
- Readback allocation remained at 2,457,600 bytes with two allocations and
  1,798 reuses. All pending work drained at completion.
- No D3D12 error or capture save failure was reported.

The machine-readable report contains source and executable hashes, the exact
command, every capture/camera sample, decoded media checks, and resource
counters in `bin/CapturePort/mp4-orbit-long-pressure-20261014-run1/report.json`.

The SDK-present launch must run with normal host permissions. A sandbox launch
fails during Streamline initialization before renderer logging because the SDK
attempts a restricted canonical-path lookup; this is documented in
`doc/windows-gui-automation.md`.
