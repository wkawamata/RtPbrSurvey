# MP4 Capture Lifecycle Validation

Date: 2026-10-14

## Scope

GPU-backed lifecycle regression coverage for standalone and GUI capture
sessions. The harness drives the native application window and verifies both
process/session state and finalized media rather than treating process exit or
file existence as success.

## Results

All 15 scenarios passed across the initial and corrected validation runs:

- Deferred close, expected failure exit code, automated capture conflict,
  restart after automated capture, restart after failure, and plan reservation
  conflict.
- Paused close without explicit Stop, warmup close, warmup Stop then close,
  draining close, and scene-switch guard.
- Resize during capture and stop/drain/restart in the same process.
- GIF finalization and EXR finalization.

The first full run exposed a harness race in the paused-close case: pause/step
input could arrive before the first output had been accepted. The harness now
waits for that accepted output before testing that pause holds the capture
clock and one frame-step advances exactly one output. The corrected case and
all remaining scenarios then passed. This was a test-harness timing issue, not
a renderer failure.

## Verification

- CMake Debug build succeeded.
- CTest passed 28/28.
- SDK-present MSBuild Debug x64 succeeded.
- GPU scenario reports:
  - Initial six: `bin/CapturePort/capture-lifecycle-full-20261014/report.json`
  - Corrected lifecycle pair: `bin/CapturePort/capture-lifecycle-20261014-final/report.json`
  - Remaining nine: `bin/CapturePort/capture-lifecycle-remainder-20261014/report.json`
- No D3D12 Debug Layer errors were reported in the completed lifecycle runs.
- Resize validation changed the native client area twice during an active
  fixed-step capture; all accepted outputs remained readable.
- Restart validation completed and drained two separate sessions in one
  process; both outputs were readable.

Generated media, reports, and logs remain under ignored `bin/CapturePort/` and
are not committed.
