# Hybrid Reflection History Work Log

This log records implementation phases and validation results for the reflection history work. Normative semantics remain in [Hybrid Reflection Contracts](hybrid-reflection-contracts.md).

## 2026-08-05: Contract and CPU State

- Extracted the focused reflection contract from the foundation note.
- Fixed `ReflectionResolvedRadiance` as linear HDR, unweighted radiance after the temporal boundary.
- Assigned history validity and ping-pong index ownership to `RtPbrSurveyEngine`, independently of swap-chain and Temporal Upscaler history.
- Added `ReflectionHistoryState` and invalidation hooks for structural and signal-changing events.
- Commits: `820b547`, `39252e2`, `381f34e`.
- Validation: Debug x64 build succeeded with zero errors.

## 2026-08-06: Resources and Identity Resolve

- Fixed the physical resource names as `ReflectionResolvedRadiance.0` and `.1`.
- Kept RenderGraph state tracking on physical names; descriptor and RTV resolvers expose semantic history/current roles.
- Reserved two persistent SRVs and two RTVs and registered both render-size resource specifications.
- Added `TemporalReflectionPass` as a full-screen identity resolve from `ReflectionEvaluatedRadiance` to the current resolved slot.
- Switched reflection-enabled `LightPass` composition to the resolved current SRV.
- History sampling, validity commit, index exchange, temporal blend, reprojection, and rejection remain pending.
- Commits: `4e57d9d`, `f413f60`, `ceb091b`.
- Validation: Debug x64 build and HLSL compilation succeeded with zero errors. DamagedHelmet rendered through the identity-resolve path. The D3D12 Debug Layer reported zero errors and zero warnings.

## 2026-08-06: History Commit and Role Exchange

- Added a per-frame pending-commit flag set only when `TemporalReflectionPass` records its identity output.
- Commit occurs after the containing command list is submitted to the direct queue, not while the frame graph is executing.
- A committed output becomes valid history and its physical slot becomes the next `historyRead`; the opposite slot becomes the next write target.
- Direct-queue ordering provides the GPU dependency between the submitted producer frame and the following consumer frame without a CPU wait.
- History sampling and temporal weighting remain pending.
- Validation: Debug x64 build succeeded with zero errors. An eight-second DamagedHelmet run exercised continuous role exchange with zero D3D12 Debug Layer errors and zero warnings.

## Next Phase

- Bind valid previous resolved radiance as a read-only history input.
- Keep the shader output identical while verifying that both physical slots alternate safely.
- Do not add temporal weighting, reprojection, or rejection until the history-read lifecycle is verified.
