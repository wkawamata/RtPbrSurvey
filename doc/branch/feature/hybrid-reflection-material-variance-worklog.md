# Hybrid Reflection Material Variance Work Log

Japanese version: [Hybrid Reflection Material Variance Work Log (Japanese)](hybrid-reflection-material-variance-worklog_j.md).

This log records diagnosis of persistent surface-wide stochastic variance in selected DamagedHelmet texture regions. Reflection signal and history semantics remain defined by [Hybrid Reflection Contracts](hybrid-reflection-contracts.md).

## 2026-08-14: Phase Start

- Started `features/hybrid-reflection-material-variance` from `main` at merge commit `a749d80` after closing the edge-stability phase.
- Reproduction regions are the rearward-facing surface adjacent to the smooth crown panel and the exposed underside pipes.
- DamagedHelmet has one glTF material, `Material_MR`, on one primitive. Treat the regions as texture-defined areas, not separate material IDs.
- The symptom covers portions of a surface rather than only edges and remains visible after camera motion stops.
- Keep stochastic sampling disabled and temporal history weight `0.0` as global defaults. The rejected-pixel neighborhood experiment also remains default-off.

## Initial Diagnostic Scope

1. Capture matched views for visible roughness, metallic, normal, evaluated radiance, resolved radiance, and temporal validity.
2. Identify whether both reproduction regions share roughness, metallic, normal-map frequency, hit/miss behavior, or high-radiance environment sampling.
3. Separate persistent input variance from history rejection. A green temporal-validity region can still retain variance when one sample per frame converges too slowly.
4. Prefer observation-only instrumentation or capture metadata before changing sampling or filtering policy.
5. If one cause is supported, test at most one default-off policy in a later measured slice.

## Out of Scope

- broad spatial denoise;
- weakening depth or normal rejection thresholds;
- DLSS Ray Reconstruction or Streamline integration;
- PathTracing;
- global-default changes;
- stacking another edge-stability policy into the previous experiment.

## Decision Gate

- If variance tracks high roughness or textured roughness, investigate sampling variance rather than material identity.
- If variance tracks normal-map frequency, verify direction stability before filtering radiance.
- If history is consistently accepted but noise persists, measure convergence and sample variance before changing rejection.
- If the two regions do not share a cause, split them rather than forcing one combined fix.

## 2026-08-14: Matched Diagnostic Capture

- Added `-ReflectionCaptureDebugView <name>` as a capture-only view selector. It preserves the existing Hybrid Reflection automation and supports `pbr-params`, `normal`, `hit-material`, `evaluated-radiance`, `resolved-radiance`, and `temporal-validity`.
- Added `capture-plan-material-variance.json` with one settling capture at frame 195. All six runs used the same camera timeline, camera distance scale `0.5`, stochastic sampling enabled, and temporal history weight `0.9`.
- The PBR capture encodes visible metallic, roughness, and ambient occlusion in RGB. The normal capture shows the visible GBuffer normal. The hit-material capture is the ray-hit payload, so it must not be interpreted as the visible-surface material.
- Evaluated radiance contains obvious surface-wide sample variance. Resolved radiance is visibly smoother, confirming that temporal accumulation is active and materially reduces the variance.
- Temporal validity is accepted across the settled frame. This does not prove every earlier frame was accepted, but it argues against persistent history rejection as the sole cause of the remaining static noise.
- The rearward region and underside pipes both contain textured PBR and normal variation. The captures do not yet justify attributing both regions to one material channel or one shared policy.

### Current Decision

- Treat the remaining symptom as slow convergence of accepted stochastic input unless a temporal sequence shows otherwise.
- Do not weaken rejection thresholds and do not enable the rejected-pixel neighborhood experiment globally.
- The next measured slice should quantify frame-to-frame variance in fixed regions of interest, separately for evaluated and resolved radiance.

### Validation

- Debug x64 MSBuild succeeded with 0 errors and the existing duplicate vcpkg import warning.
- Six automated captures completed successfully.

## 2026-08-14: Fixed-ROI Temporal Variance

- Captured evaluated and resolved radiance at frames 195 through 300 in 15-frame intervals. The camera remains fixed after frame 180; stochastic sampling and temporal history weight `0.9` match the previous diagnostic capture.
- Added `Measure-MaterialVariance.ps1` to calculate display-space luminance temporal standard deviation over the eight-frame series. This is a repeatable screenshot symptom metric, not an HDR resource measurement.
- The user confirmed both annotated rectangles. After the initial approximate confirmation, `rearward_surface` was reduced and moved right and upward to isolate the reported rearward area adjacent to the crown; `underside_pipes` continues to cover the reported lower pipes.
- `rearward_surface`: mean temporal standard deviation changed from `0.04266` evaluated to `0.00964` resolved, a `77.40%` reduction. The resolved/evaluated ratio is `0.2260`.
- `underside_pipes`: mean temporal standard deviation changed from `0.03113` evaluated to `0.00825` resolved, a `73.51%` reduction. The resolved/evaluated ratio is `0.2649`.
- The close reduction ratios support one shared observation: temporal accumulation is effective in both regions but leaves roughly one quarter of the evaluated display-space temporal deviation. They do not prove that the underlying material or sampling cause is identical.

### Decision

- Keep current rejection thresholds and default-off neighborhood policy unchanged.
- Use the two confirmed ROIs as independent acceptance metrics for later sampling or filtering experiments.
- Before adding a policy, the next diagnostic should compare convergence versus history weight or elapsed settled frames. This distinguishes insufficient history length from variance that requires a different estimator or spatial information.

## 2026-08-14: History-Weight Convergence

- Captured the same fixed-camera eight-frame series with resolved history weights `0.0`, `0.5`, and `0.98`; reused the existing `0.9` series and evaluated-radiance reference.
- All eight `0.0` PNG files are byte-identical to their evaluated-radiance counterparts. This confirms that zero history weight selects the current evaluated sample without altering the displayed result.
- Added `Measure-HistoryWeightConvergence.ps1`. It reports full-series deviation, early-window deviation at frames 195-225, late-window deviation at frames 270-300, and early-to-late mean displayed-luminance drift.
- `rearward_surface` full-series deviation: evaluated/`w0` `0.04266`, `w50` `0.02282`, `w90` `0.00964`, `w98` `0.01068`. Late-window deviation reaches `0.00663` at `w90` and `0.00267` at `w98`.
- `underside_pipes` full-series deviation: evaluated/`w0` `0.03113`, `w50` `0.01904`, `w90` `0.00825`, `w98` `0.00846`. Late-window deviation reaches `0.00602` at `w90` and `0.00221` at `w98`.
- `w98` has the lowest late-window variance but larger early-to-late luminance drift: `-0.00829` for `rearward_surface` and `-0.00438` for `underside_pipes`, compared with `-0.00192` and `-0.00062` at `w90`.

### Decision

- Do not raise the default history weight to `0.98`. Its lower late variance is paired with measurable slow settling, and its full-series variance is slightly worse than `0.9` in both confirmed regions.
- Retain `0.9` as the current validation setting. It is the best balance measured here, not a declaration that `0.9` is the final production policy.
- The remaining late-window variance at `0.9` supports a later bounded estimator or spatial-information experiment more than simply extending history further.

## 2026-08-15: Surface-Variance Spatial Experiment

- Added one default-off experiment, `Surface Variance Filter`, at the current-sample boundary before temporal blending. It applies a 3x3 filter to evaluated radiance while accepting neighbors only when visible depth, normal, roughness, and metallic are similar. Near-perfectly smooth visible surfaces bypass the filter.
- The experiment does not alter the unweighted radiance contract, history weight, history rejection thresholds, or LightPass contribution weighting. The earlier rejected-pixel neighborhood fallback remains separate and default-off.
- Added capture-only CLI flag `-ReflectionSurfaceVarianceFilter`, UI controls, settings persistence, and the required GBuffer PBRParams read dependency. Debug noise is injected per accepted neighbor using the existing deterministic rule.
- Debug x64 MSBuild succeeded with 0 errors and the existing duplicate vcpkg import warning. The eight-frame filtered capture series completed successfully.
- A filter-enabled D3D12 Debug Layer capture logged no errors. It reported only the two pre-existing committed-buffer initial-state warnings.
- At history weight `0.9`, mean temporal deviation relative to the temporal-only baseline decreased by `51.20%` in `rearward_surface` and `53.49%` in `underside_pipes`. Mean displayed luminance changed by approximately `+0.00153` and `+0.00108`, respectively.
- Added a persistent bilingual HTML suite with early, middle, and late settled A/B cases. User report `hybrid-reflection-material-variance-filter-v1-report-2026-08-14T21-39-51.610Z-a66a127d.json` passed all 9 criteria with no selected defects and no notes.

### Decision

- Keep the experiment implemented and default-off. It passes the static-region objective and subjective gates, but this phase has not yet established motion/disocclusion behavior strongly enough to enable it by default.
- Do not stack another estimator or widen the filter in this phase. The next priority is paired linear-HDR variance diagnosis; detailed object-motion diagnosis is conditional on a practical Lit-view problem remaining after the estimator work.

## 2026-08-15: Interactive Resolved-Radiance Review

- The user reviewed `ReflectionResolvedRadiance` interactively in the executable, rather than relying only on the HTML still captures.
- At history weight `0.9`, static noise was visibly reduced. Enabling `Surface Variance Filter` further reduced static noise on the reported noisy material regions.
- Static viewing and motion reversal showed no objectionable problem in this review.
- With a strong history weight, the user perceived delayed reflection updating during object rotation in the reflection-only debug view. The delay was much less noticeable in the Lit composite. The previously stated value of approximately one second was an impression, not a measurement.

### Updated Decision

- Record object-rotation latency as an unquantified low-to-medium-priority risk, not a confirmed defect and not evidence that the static surface filter failed. The filter operates on the current sample before temporal blending and does not itself retain prior frames.
- Do not enable the filter or history setting globally based on static results alone. If later required, measure object-rotation response separately in Evaluated Radiance, Resolved Radiance, Temporal Validity, and Lit views.
- Avoid immediately lowering history weight or weakening all history. A future diagnosis must distinguish expected EMA response, object-motion reprojection/depth validation, normal rejection, reflection-hit changes, and the lower perceptual contribution in the Lit composite.

## Review Disposition and Branch Boundary

- The eight-frame tone-mapped PNG measurements are preliminary symptom diagnostics. They do not establish HDR-domain variance, long-run mean preservation, estimator bias, energy conservation, or agreement with a physical reference.
- The 9/9 static subjective pass shows that no objectionable side effect was found in the reviewed images. It does not prove the filter is production-ready.
- Use the term `High-SPP Current-Estimator Mean Baseline`, not `sample reference`, for a 64/256/1024-sample average of the current approximation. This baseline can measure variance and convergence but cannot establish physical correctness.
- Close this branch as material-variance diagnosis plus one bounded default-off experiment. Do not add another filter or promote current defaults here.
- The recommended next branch is `features/hybrid-reflection-hdr-variance-diagnostics`: paired linear-HDR statistics for the confirmed ROIs, hit/miss and hit-distance tracking, temporal validity, current-estimator mean baselines, and optional object-motion timelines after the higher-priority static measurements.

## 2026-08-15: Phase 0 Final Acceptance Audit

Phase name: **Material-region variance diagnosis and bounded surface-filter experiment**. The reproduced areas are texture regions within the material presentation; this phase does not claim that a distinct material ID is the cause.

| Classification | Gate | Evidence |
| --- | --- | --- |
| PASS | Default-off policy | `HybridReflectionSettings::surfaceVarianceFilterEnabled` initializes to `false`; capture automation enables it only with `-ReflectionSurfaceVarianceFilter`. |
| PASS | Disabled code path | `shaders_TemporalReflection.hlsl` enters `FilterSurfaceVariance` only when `g_surfaceVarianceFilterEnabled != 0`; otherwise the evaluated sample proceeds directly to the existing temporal path. |
| PASS | Existing default behavior | Stochastic sampling remains disabled, temporal history weight remains `0.0`, and both spatial experiments remain disabled by default. |
| PASS | Debug x64 and HLSL build | A forced Debug x64 `Rebuild` succeeded with 0 errors. All custom HLSL entries, including `shaders_TemporalReflection.hlsl`, were rebuilt. The only build warning was the pre-existing duplicate vcpkg import warning. |
| PASS | D3D12 Debug Layer | Current matched filter-off and filter-on automation logged 0 errors. Both logged the same two known committed-buffer initial-state warnings; no new warning was introduced by enabling the filter. |
| PASS | Documentation consistency | English and Japanese worklogs carry the same decisions. The contract identifies the filter as a default-off bounded experiment, not a production denoiser. |
| PASS | Rotation wording | The perceived delay is recorded as unquantified and more visible in the reflection-only debug view; the contrary observation that it is much less noticeable in Lit is retained. |
| PASS WITH LIMITATION | Scoped subjective suite | The saved report passed 9/9 static criteria for the reviewed DamagedHelmet captures. This applies only to the selected frames, ROIs, view, and settings. |
| PASS WITH LIMITATION | Runtime equivalence evidence | The current filter-off capture completed with the expected two known Debug Layer warnings and no errors. Exact comparison with the historical pre-filter PNG series was not accepted because the documented camera-distance setting did not reproduce the historical framing; code-level bypass is the stronger verified evidence for this gate. |
| NOT CLAIMED | Production readiness | No production default or production denoiser policy is established. |
| NOT CLAIMED | Physical correctness | The phase does not establish estimator correctness, unbiasedness, energy conservation, or convergence to a physical reference. |
| NOT CLAIMED | Generalization | Results are not generalized beyond the evaluated scene, ROIs, sampled frames, and settings. |
| NOT CLAIMED | Quantitative motion latency | No one-second delay or other settling duration is claimed; T50/T90/T95 were not measured. |

Fixed conclusion:

> The default-off filter reduced static display-space variance in the evaluated test ROIs and passed the scoped subjective suite. This does not establish estimator correctness, production denoiser readiness, or generalization beyond the evaluated conditions.

Phase 1 handoff: create `features/hybrid-reflection-hdr-variance-diagnostics` after this diagnostic branch is integrated. Use 64 frames for development, 256 for the standard PR gate, and 1024 only for extended mean-drift, rare-firefly, or bias-trend auditing. Paired runs must reset the sample index, camera, animation, and history at the same points; keep all non-filter settings and measured frame ranges identical, and separate warm-up from measurement.
