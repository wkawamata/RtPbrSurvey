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

## 2026-08-06: Read-Only History Wiring

- Added a dedicated root-signature SRV slot for previous `ReflectionResolvedRadiance` and a one-DWORD history-valid constant.
- The first frame after invalidation does not bind or declare a history read. Valid frames declare the selected physical history slot as a pixel-shader read and bind its semantic SRV.
- The identity shader consumes history alpha, which is opaque by contract, while preserving current-frame RGB. No temporal RGB weighting was introduced.
- The current and history physical slots remain distinct throughout graph execution; role exchange still occurs only after direct-queue submission.
- Validation: Debug x64 build and HLSL compilation succeeded with zero errors. DXIL inspection confirmed the `space11` history `TextureLoad` and `b5` validity constant remain in the compiled pixel shader. An eight-second DamagedHelmet run exercised invalid-to-valid history and continuous ping-pong with zero D3D12 Debug Layer errors and zero warnings.

## 2026-08-06: Unreprojected Weighting Experiment

- Added one persisted Debug UI control, `Temporal History Weight`, with a range of `[0, 0.98]` and a default of zero.
- Valid history uses `lerp(currentEvaluated, previousResolved, historyWeight)`. A zero weight preserves identity behavior.
- Changing the weight invalidates history so a new experiment does not inherit samples accumulated under the previous coefficient.
- The UI explicitly labels the blend as lacking motion reprojection and warns that higher values trade static stability for motion trails.
- Reprojection, rejection, confidence/history-length state, and spatial denoise remain absent.
- Validation: Debug x64 build and HLSL compilation succeeded with zero errors. DXIL inspection confirmed the history RGB load and weight arithmetic remain in the compiled shader. An eight-second default-weight DamagedHelmet run exercised the path with zero D3D12 Debug Layer errors and zero warnings. Nonzero-weight visual comparison remains a manual test-scene task.

## 2026-08-06: Evaluated/Resolved Observation View

- Added a `ReflectionResolvedRadiance` render debug mode beside `ReflectionEvaluatedRadiance`.
- Selecting the resolved view schedules Evaluate and Temporal passes even when final contribution is disabled.
- The existing reflection debug shader samples the current resolved SRV through its radiance input; no comparison buffer or additional rendering pass was introduced.
- RenderGraph tracking uses the current physical write-slot name, preserving the stable-name rule and queue-submit role exchange.
- Validation: Debug x64 build succeeded with zero errors. Automated UI validation exposed the known duplicate DamagedHelmet load assertion when `Load Scene` was clicked after `-AutoSelectGltfDamagedHelmet`; the test process and generated log were removed. A clean nonzero-weight visual comparison remains pending.

## 2026-08-06: Temporal Observation Protocol

Use DamagedHelmet with Hybrid Reflection and Reflection Contribution enabled. Keep lighting, material, camera pose, render size, and contribution controls unchanged while changing only `Temporal History Weight`. For every weight, wait at least two seconds after the automatic history reset before judging the static result.

| Weight | Static observation | Controlled camera observation | Purpose |
|--------|--------------------|-------------------------------|---------|
| `0.0` | Capture `Evaluated Radiance` and `Resolved Radiance`; they should match. | Slowly orbit horizontally, then stop. | Identity baseline and debug-view wiring check. |
| `0.5` | Compare high-frequency highlights and noisy edges against the baseline. | Repeat the same orbit and stop. | Detect the first visible stability benefit and one-frame lag. |
| `0.9` | Record highlight stability and convergence time after reset. | Repeat the orbit; inspect silhouette edges and newly revealed background. | Expose trails, lag, and disocclusion contamination. |
| `0.98` | Record slow convergence and whether old highlights remain visible. | Repeat the orbit and reverse direction once. | Stress-test long persistence and direction-change trails. |

For each row, record:

- whether static variance visibly decreases in `Resolved Radiance` relative to `Evaluated Radiance`;
- the approximate time for the resolved image to settle after reset or camera stop;
- where trails occur: object silhouette, reflection feature, or background disocclusion;
- whether contamination is local or spreads across smooth reflective regions;
- whether returning to weight `0.0` immediately restores evaluated/resolved equivalence after reset.

The automated observation attempt could not complete after the known duplicate DamagedHelmet load assertion. Subsequent Debug exe launches left a live process with `MainWindowHandle=0`, so Computer Use had no targetable window. All test processes and generated logs were removed. No visual result is claimed from that run.

## Decision Gate After Observation

- If `0.5` already produces objectionable trails, motion reprojection is required before any further history weighting work.
- If reprojection aligns most pixels but newly revealed regions remain contaminated, depth/normal disocclusion rejection is the next minimum requirement.
- Hit distance, hit flag, hit normal, and visible roughness should be added to rejection only when the recorded failures show that surface depth/normal alone cannot separate reflection-signal changes.
- Spatial denoise and confidence/history-length buffers remain deferred until reprojection and minimum rejection are measured.

## Next Phase

- Execute the matrix in a clean app session and fill in the observation fields.
- Derive the minimum reprojection inputs and rejection order from the recorded failures.
- Revisit the overall phase size at this boundary, as previously requested; the plan remains intentionally unshrunk for now.
