# Capture Session Contract Fixes

Depends on PR #79, which depends on PR #78. Do not merge the dependency branch into main implicitly.

- Fixed-step readiness stays false from request preparation through readback completion. Hosts gate physics/camera at the start of the loop, update capture scheduling after scene updates, and continue rendering/polling even when simulation is blocked. No GPU wait is added.
- Duration starts at Recording, excluding warmup. This corrects the unreleased duration API so changing warmup does not shorten the recording. Stop, frame limit, and duration drain accepted work before reaching a terminal state.
- Session requests carry nonzero request IDs. The renderer queue preserves IDs on success, immediate failure, and shutdown. SceneRenderer legacy requests use ID zero and cannot consume the active session result. Start rejects a busy screenshot queue or an active session. Direct EngineForDebugTools queue access bypasses this host contract.
- `Platform/CommandLineOptions.h` exposes `bool BuildCaptureSessionConfig(const CommandLineOptions&, RtPbrSurvey::CaptureSessionConfig&, std::string&)`. Include `Runtime/CaptureSession.h` for the complete config type. Both CLI and UI ultimately use core Start validation. The CLI helper handles format/source conversion, ROI, and optional duration; Start remains required for semantic validation.
- `-CaptureSessionRoi x y width height` and `-CaptureSessionDurationSeconds seconds` are available to standalone and external hosts. Legacy `-CapturePath` behavior is retained.
- PNG/EXR readback Map ranges use the actual buffer extent, fixing unaligned ROI widths whose final row has no trailing pitch padding.

Validation: CMake SceneRenderer and focused Screenshot, ScreenshotRequestQueue, CaptureSession tests; standalone Debug x64 MSBuild; duration-only real-time CLI smoke with a 100x80 ROI, five PNG outputs and no D3D12 error. Interactive panel manipulation and deterministic standalone simulation timing were not validated. GIF/MP4 and PT-specific sequence scheduling remain outside this change.

## External Host Validation

Tank integration reported the following results with renderer commit `817938f`:

- Debug build succeeded; seven GPU smoke cases passed: full PNG, 317x239 ROI,
  warmup90 excluded from duration, EXR, and rejection of GIF, invalid FPS, and
  out-of-bounds ROI.
- Fixed60 simulation advanced by exactly 1/60 per accepted step while render-loop
  iterations continued during pending readback (observed loop count 3 to 6).
- Successful cases had no D3D12 ERROR. Actual GUI click interaction was not tested.
- EXR deployment required transitive `miniz.dll`, supplied by vcpkg app-local deployment.

These are host-reported results, not an additional standalone rerun. See
[Capture Runtime Dependencies](../refactor/cmake-host-integration.md#capture-runtime-dependencies)
for deployment requirements and the proposed, not yet implemented, missing-runtime check.
