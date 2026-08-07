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

### Completed Observation

A later clean session loaded DamagedHelmet exactly once and exercised the `ReflectionResolvedRadiance` view with controlled horizontal camera orbits.

| Tested weight | Observation |
|---------------|-------------|
| `0.0` | Resolved output followed camera motion as a single image with no visible history trail. It is the correct identity baseline. |
| approximately `0.5` | Motion lag was visible but decayed quickly. No compensating static-noise benefit was visible because the current one-ray signal is deterministic in a static scene. |
| approximately `0.91` | Camera motion produced clear multiple-image trails at the helmet silhouette and internal reflection features. Newly revealed background retained prior object radiance temporarily. |
| `0.98` | Direction reversal left a strong old-pose image and long-lived contamination. Convergence after motion was visibly slow. |

The observed failure is screen-space history reuse without correspondence. It affects both the object silhouette and valid pixels inside the object; disocclusion-only rejection therefore cannot solve it. Motion reprojection is required before any nonzero weight can become a default. Depth/normal rejection is then required for newly revealed regions and correspondence failures.

The current deterministic signal provides no measured reason to enable accumulation by default. Keep `Temporal History Weight = 0` outside explicit experiments until stochastic reflection sampling or another varying signal creates a stability problem worth trading history for.

## Decision Gate After Observation

- If `0.5` already produces objectionable trails, motion reprojection is required before any further history weighting work.
- If reprojection aligns most pixels but newly revealed regions remain contaminated, depth/normal disocclusion rejection is the next minimum requirement.
- Hit distance, hit flag, hit normal, and visible roughness should be added to rejection only when the recorded failures show that surface depth/normal alone cannot separate reflection-signal changes.
- Spatial denoise and confidence/history-length buffers remain deferred until reprojection and minimum rejection are measured.

Decision from this observation: the first gate failed at approximately `0.5`; proceed to a small reprojection/rejection contract phase, not to stronger weighting or denoise.

## 2026-08-06: Reprojection/Rejection Contract Audit

- Confirmed `GBuffer.MotionVector` stores `previousNdc - currentNdc` in `R16G16_FLOAT` and includes camera plus `prevWorld` object motion.
- Identified two non-geometric additions in the stored value: jitter cancellation and the Temporal Upscaler debug value offset. Reflection must subtract both before UV conversion.
- Fixed the NDC-to-UV mapping, including the inverted Y sign, and made bounds rejection the first sample-validity test.
- Confirmed current depth/normal alone cannot validate a previous-frame sample. Previous visible depth and world-space normal require auxiliary history under the same reflection-history ownership.
- Kept resolved-radiance alpha opaque and excluded confidence/history length from it.
- Recorded the moving-object limitation: XY motion exists, but exact previous-depth prediction is unavailable from the current GBuffer contract.
- No code, shader, resource, or descriptor changes were made; build was not rerun because this phase is documentation-only.

## 2026-08-06: Motion Reprojection and Bounds Rejection

- Added `GBuffer.MotionVector` and Camera CBV as read-only `TemporalReflectionPass` inputs.
- Recovered raw `previousNdc - currentNdc` by subtracting jitter cancellation and debug value offset.
- Converted raw NDC displacement to previous-frame UV with the contract Y inversion.
- Rejected out-of-bounds history and used nearest integer history sampling for the first correspondence experiment.
- Kept the default history weight at zero and did not add depth/normal resources or rejection.
- Validation: Debug x64 and HLSL compilation succeeded with zero errors. DXIL inspection confirmed motion-vector load, camera correction fields, and reprojected history load remain in the compiled shader. Automated visual orbit validation could not acquire a targetable app window in this session, so no visual improvement is claimed yet.

## 2026-08-07: Reprojection Visual Validation Retry

- Started with a clean process audit and attempted a normal DamagedHelmet validation session through Computer Use.
- One stale/reused session exposed the existing `SampleScene.cpp:55` duplicate-load assertion and was terminated without continuing.
- After terminating every `RtPbrSurvey` process, a single normally launched process remained responsive but did not publish a targetable window to `list_windows()` after an additional eight-second wait.
- No stale window handle or coordinate was reused. The process was terminated and no generated log or output was retained.
- Reprojection visual quality remains unverified. Depth/normal history implementation must not start until the camera-orbit comparison can be completed or an equivalent deterministic capture path is available.

## Next Phase

- Restore a reliable single-instance visual test session or add a separately reviewed deterministic capture route.
- Run the approximately `0.9` camera-orbit comparison before authorizing auxiliary rejection resources.
- Revisit the overall phase size now: progress is blocked by validation infrastructure rather than reflection-contract size.

## Deferred Temporal Noise Validation

- The current static reflection signal is deterministic, so it cannot demonstrate a temporal noise-reduction benefit by itself.
- Add a small debug-only, default-off synthetic-noise experiment before judging accumulation quality. Apply deterministic per-pixel/per-frame, zero-mean luminance modulation to the current evaluated-radiance input; do not inject noise into resolved history.
- Compare history weights `0.0` and approximately `0.9` with a static camera, then with controlled camera motion. The acceptance checks are reduced flicker, preserved mean luminance/color, and no unbounded conversion of noise into trails.
- Treat this as a Temporal pass isolation test. A later physically meaningful test should use stochastic rough-reflection sampling with a bright compact emitter, but stochastic ray generation is not part of the current History contract phase.
- User-facing visual review may use a small set of labeled screenshots and simple pass/fail questions for tracking, silhouette trails, direction-reversal persistence, screen-edge artifacts, and settling after motion.

## Deterministic Screenshot Route

- Reuse the existing asynchronous `SceneRenderer` screenshot readback instead of adding another capture implementation.
- `-CapturePath <path>` requests one PNG after the configured warm-up count, `-CaptureAfterFrames <count>` selects that count, and `-ExitAfterCapture` closes the app only after the screenshot result is available.
- The route captures a stable frame without requiring a targetable Computer Use window. It does not automate camera motion or alter reflection settings.
- Validation: Debug x64 built with zero errors. A DamagedHelmet run with 30 warm-up frames produced `Screenshots/history_capture_smoke.png`, the PNG contained the rendered scene, and the process exited after the asynchronous capture completed.

### Static Resolved-Radiance A/B Controls

- `-CaptureReflectionResolvedRadiance` applies Deferred rendering, enables Hybrid Reflection, and selects the resolved-radiance debug view after the saved scene configuration is loaded.
- `-ReflectionTemporalWeight <weight>` supplies a capture-only history-weight override clamped to the supported `[0.0, 0.98]` range.
- Automated capture sessions do not save scene configuration on shutdown, so validation overrides cannot replace the user's persisted interactive settings.
- These controls support static weight `0.0` versus nonzero A/B captures. Camera-motion automation remains outside this step.
- Validation: Debug x64 built with zero errors. DamagedHelmet resolved-radiance captures after 60 warm-up frames at weights `0.0` and `0.9` produced byte-identical PNG files (matching SHA-256), confirming that the current static deterministic signal has no temporal variance to reduce. The logged run contained no D3D12 errors and two pre-existing buffer initial-state warnings.

### Deterministic Camera-Orbit Capture

- `-ReflectionOrbitDegrees <degrees>` and `-ReflectionOrbitFrames <frames>` apply a horizontal Arcball orbit whose final incremental step occurs on the captured frame.
- The orbit is limited to the automated resolved-radiance capture path and reuses the camera controller's explicit object-viewer state API.
- The screenshot request is queued before the captured `RunFrame`, after the requested number of completed warm-up frames. This preserves nonzero camera motion on the captured frame instead of photographing the first stationary frame after the orbit.
- The requested orbit-frame count is clamped to the available warm-up-plus-capture interval. No general input recording or camera-animation system is introduced.
- Validation: Debug x64 built with zero errors. A 20-degree horizontal orbit over 30 frames produced matched-pose captures at weights `0.0` and `0.9`. The nonzero-weight image visibly softened the silhouette and internal detail along the motion direction; final visual acceptance is reserved for the user-facing A/B pass/fail review. The logged run contained no D3D12 errors and only the two pre-existing buffer initial-state warnings.

#### User A/B Acceptance

The user compared saved full-resolution captures A (`weight = 0.0`) and B (`weight = 0.9`) and marked all three checks as true:

- B has a softer silhouette than A.
- B retains motion history in internal detail as well as at the silhouette.
- B's quality is not acceptable as the current high-weight result.

Decision: motion reprojection and bounds rejection alone are insufficient at high history weight. Proceed to the minimum depth/normal history and rejection phase before considering stronger accumulation, synthetic-noise tuning, or spatial denoise.

## 2026-08-07: Minimum Depth/Normal Rejection Implementation Audit

- Confirmed current visible depth is exposed as `R32_FLOAT` from the typeless depth resource and current visible normal is an unencoded world-space vector in `R16G16B16A16_FLOAT`.
- Selected two auxiliary ping-pong pairs: `ReflectionHistoryDepth` (`R32_FLOAT`) and `ReflectionHistoryNormal` (`R16G16B16A16_FLOAT`). They share the resolved-radiance validity, read index, invalidation, resize lifecycle, and post-submit role exchange.
- Selected a three-target `TemporalReflectionPass` output: resolved radiance, current depth copy, and current world normal copy. This avoids a separate copy pass and guarantees that all history fields describe the same submitted frame.
- Previous depth validity will compare stored previous device depth against the current world position reconstructed with `invViewProj` and projected by `prevViewProj`. Current device depth must not be compared directly with previous device depth under camera motion.
- Previous normal validity will compare normalized world-space vectors at the nearest reprojected pixel. Depth and normal validity remain point decisions even if radiance filtering changes later.
- Depth and normal thresholds are policy constants/settings, not resource semantics. The first implementation may use conservative fixed values, but tuning and user controls are separate from resource ownership.
- Moving geometry remains approximate because the current contract provides XY motion but no exact previous-world-position or previous-clip-depth signal.
- No hit distance, roughness, confidence, history length, synthetic noise, or spatial-denoise resource is authorized in this slice.

## 2026-08-07: Minimum Depth/Normal Rejection Implementation

- Added two persistent `R32_FLOAT` depth-history slots and two persistent `R16G16B16A16_FLOAT` world-normal-history slots under the existing reflection read index and validity state.
- Extended `TemporalReflectionPass` to three MRT outputs so resolved radiance, current visible depth, and current visible normal are committed from the same frame and exchange roles together after submission.
- Bound previous depth and normal only when reflection history is valid. The first frame after invalidation writes all three current signals without reading stale history.
- Reconstructed the current world position from current device depth and `invViewProj`, projected it with `prevViewProj`, and accepted history when the previous-device-depth difference is at most `0.002` and the world-normal dot product is at least `0.9`.
- Kept nearest validity sampling, default history weight zero, opaque resolved-radiance alpha, and the documented moving-geometry limitation.
- Validation: Debug x64 and HLSL compilation succeeded with zero errors. The 20-degree/30-frame DamagedHelmet orbit at weight `0.9` completed, produced a PNG, and logged zero D3D12 errors plus the two pre-existing buffer initial-state warnings. The new image is visibly sharper than the previously rejected unrejected B image; final A/B acceptance remains pending.
