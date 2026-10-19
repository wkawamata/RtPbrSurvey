# Path tracing history validation (Step 6)

## Scope

- [x] Compare normalized effective settings before invalidating history.
- [x] Add real Engine API tests for setting changes and equivalent clamped requests.
- [x] Preserve history when only samples per frame changes.
- [x] Test manual reset while paused, resume, and reset-reason classification.
- [x] Verify nonzero GPU sample counts across pause/resume and reset.
- [x] Verify camera/light/material/instance-transform/resolution changes against fresh-history HDR captures.
- [x] Verify accumulation OFF sample indexing and accumulation-mode history contamination controls.

## Effective settings policy

Bounce count and samples per frame are clamped to [1, 16]. Environment sampling
mode is capped at 7 and emissive sampling mode at 2. Reset decisions compare these
effective values, not unvalidated requests. For example, requesting 100 bounces
when the effective value is already 16 must not clear history.

Changes to accumulation mode, bounce count, seed, direct/environment/emissive
lighting, sampling modes, Russian roulette or debug output invalidate history.
Changing only the sample batch size preserves history. Pause/resume does not
invalidate history; manual reset retains the paused flag while clearing CPU
count, sample index and validity and requesting a GPU clear.

## Automated coverage

2026-10-06: CMake Debug build succeeded, CTest passed 26/26 and the existing
PathTracing Python suite passed 96/96. No GPU runtime campaign was run for this
initial API-policy change.

`Tests/PathTracingHistoryTests.cpp` constructs the existing Engine without GPU
initialization and calls public APIs. It checks all history-affecting PT settings,
clamped equivalence, batch bounds, pause/reset/resume, unchanged and changed
camera/scene/rendering path/lighting, and their reset reasons.

These CPU tests alone are not evidence that a nonzero GPU image or count is
preserved. The runtime campaign below separately covers dispatch accounting and
manual GPU clearing. Material uploads and resource resize still require their
own changed-state campaign, completed below on 2026-10-08.

## GPU lifecycle campaign (2026-10-07)

`-PathTracingHistoryValidation <directory>` runs a dedicated CLI-only timeline
timeline and exits after all captures finish. It requires `-SceneFile` and
`-LogToFile`, and rejects other capture automation. Normal GUI execution is
unchanged. Seed 11 is fixed; sample batches and accumulation mode are controlled
by the timeline. The original nine-stage lifecycle campaign kept fixture lighting/
materials unchanged; the current nineteen-stage version adds the mutations below.

The baseline is asset 01, at 1920x1080 on RTX 2080 Ti / driver 616.56. Every
checkpoint is a float32 RGBA `PathTracing.Accumulation` PTBUF capture. The reader
checks every pixel's alpha against the expected CPU sample count, then divides
RGB sums by alpha for positive counts. A zero count requires all RGBA values to
be exactly zero. The existing PFM zero-sample rejection is retained.

| Checkpoint | CPU and GPU sample count | Next sample index | Batch |
| --- | --- | --- | --- |
| Baseline | 16 | 16 | 1 |
| Paused for at least three render frames | 16 | 16 | 1 |
| Resumed | 32 | 32 | 1 |
| Manual reset while paused | 0 | 0 | 1 |
| Reset and replay | 16 | 16 | 1 |
| Batch-size change without reset | 32 | 32 | 4 |
| Accumulate OFF, four frames | 3 | 12 | 3 |
| Accumulate OFF after reset, four frames | 3 | 12 | 3 |
| Fresh history after returning to Accumulate ON | 32 | 32 | 1 |

All nine checkpoints passed. Pause preserves both pixels and index; reset replay
at 16 samples exactly matches baseline. Resume to 32 samples exactly matches a
fresh-history 32-sample run. The non-accumulated replay also matches exactly.
Reset while paused has maximum absolute RGBA value 0. Batch-4 vs batch-1 has
maximum normalized RGB error 1.788139343e-7 and relative RMSE 4.449285370e-7,
within predeclared limits 2e-6 * max(1, reference maximum) and 1e-6 respectively.
Floating-point addition grouping is allowed only for this batching comparison.

CMake Debug succeeded; CTest passed 26/26 and Python passed 107/107. D3D12
ERROR/CORRUPTION count is zero; three existing buffer InitialState warnings were
retained. Curated evidence is
`path-tracing-validation-results/completion-step-6-history-lifecycle-summary.json`.
Raw buffers/logs are untracked under `bin/PathTracingValidation/completion-history-20261007-r3`.

The initial r1 attempt stopped at reset because normalized PFM correctly rejected
zero samples. r2 passed with mixed PFM/raw capture; r3 is the authoritative run
checking raw GPU alpha at every stage. Failures now log and close without a modal
exception dialog. Python also requires the completion marker and every ordered
successful capture, so a normal process exit alone cannot become a false pass.

Limitations: this is one static fixture on one GPU. Fresh history is a manual reset
in the same process, not an independent renderer. It does not validate arbitrary
animation, long-running precision/overflow, or camera/light/material/geometry/
resolution invalidation. The original run was followed by the changed-state campaign below.

## Changed-state campaign (2026-10-08)

The nineteen-stage extension first repeats all nine lifecycle checkpoints, then
renders five changed-state/manual-reset pairs at 16 samples. Old history is already
nonzero before every mutation. Immediate count/index/validity and reset reason are
checked through the normal public API; resize is checked after its deferred apply.
Raw GPU alpha is verified at every pixel for all nineteen captures.

| Mutation | Changed vs manual-reset replay | Mutation signal RGB RMSE |
| --- | --- | ---: |
| Lens Shift X +0.12 | Exact | 0.06024017974 |
| Add downward directional light, intensity 0.7, RGB (0.2,0.5,1) | Exact | 0.05301488181 |
| Receiver roughness 0.25 / metallic 0.5 | Exact | 0.03979527113 |
| Receiver instance translation X +0.75 | Exact | 0.01585588446 |
| Resize 1920x1080 to 1280x720 | Exact at requested dimensions | Not compared across resolutions |

Mutation signal compares same-resolution 16-sample images before and after each
change, requiring RMSE >1e-5. It prevents a no-op mutation from becoming a false
pass. Resize instead requires CPU and captured resource dimensions of 1280x720.

The first run exposed a production bug: `CameraStatesEqual` omitted lensShiftX/Y.
Lens Shift changed the projection without invalidating history. Both fields are
now compared, with Engine API tests for X, Y and unchanged values. The r1 failure
is retained under ignored bin outputs; r2 is the successful authoritative run.

Material change reports Material immediately. On its first render frame, the
existing emissive-table source refresh changes the final reason to Scene. The
validation records both entry and final reasons and permits this specific
secondary refresh; it does not weaken the count/index/image checks. No emitter-
table redesign is included.

CMake Debug build passed, CTest passed 26/26, Python passed 111/111. All nineteen
captures passed, with zero D3D12 ERROR/CORRUPTION and three existing InitialState
warnings. Evidence:
`path-tracing-validation-results/completion-step-6-history-changes-summary.json`.
Raw outputs: `bin/PathTracingValidation/completion-history-changes-20261008-r2`.

Step 6 is complete for this measured single-GPU fixture domain. This is not a
claim covering every editing operation: topology/vertex-buffer replacement,
animated/skinned meshes, environment reload, all material texture mutations,
long-running precision/overflow and cross-GPU replay remain outside this campaign.
The fresh-history reference uses manual reset in the same process, not an
independent renderer. Next is Step 7 performance measurement.

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
python -B Tests/PathTracing/validate_history.py --output bin/PathTracingValidation/history-repeat
```
