# features/hybrid-reflection-variance-guided-processing

## Objective

Design and validate variance-guided processing for the weighted Hybrid Reflection estimator signal established in Phase 2. Begin with signal ownership, variance observability, and bounded policy experiments. Do not promote the existing 3x3 Surface Variance Filter or claim production-denoiser readiness from variance reduction alone.

## Starting Point

- Phase 2 is integrated into `main` as `105f6da` through PR #33.
- `ReflectionSpecularEstimate` is the current-frame linear-HDR signal containing the correlated incident-radiance sample, Cook-Torrance BRDF, cosine, and directional-PDF throughput.
- Existing Temporal Reflection accumulates the unweighted `ReflectionEvaluatedRadiance`; LightPass later applies visible-surface Fresnel and roughness policy. This path remains intact until a separate weighted-history transition is validated.
- The default-off Surface Variance Filter is a fixed 3x3 pre-temporal filter over unweighted Evaluated Radiance. It has no measured variance estimate and must not be relabeled variance-guided.
- The scoped Lit orbit showed slight temporal noise that the user considered likely acceptable. Dynamic temporal diagnosis therefore remains conditional rather than the first task.

## Initial Contract Direction

The variance-guided path should ultimately operate on the weighted estimator signal, not mix an unweighted radiance history with current-frame BRDF/PDF throughput. The intended future signal is `ReflectionResolvedSpecularEstimate`: a temporally resolved form of `ReflectionSpecularEstimate` that retains linear-HDR weighted-specular semantics.

Variance metadata must remain distinct from radiance alpha. The first candidate contract is a separate temporal-moments history containing luminance first and second moments, with history validity and rejection following the same ownership/reset lifecycle as the resolved weighted estimate. A history-length or confidence signal may be added only if a measured adaptive policy needs it.

No resource is committed by this initial note. Format, precision, and whether moments are computed before or after temporal clipping remain audit decisions.

## First Work Unit

1. Audit the exact producer/consumer transition from `ReflectionSpecularEstimate` to a future resolved weighted estimate and LightPass without double-applying Fresnel or roughness weighting.
2. Define temporal moment update, rejection/reset behavior, and diagnostic debug meaning.
3. Add observability before adaptive filtering: mirror control should report near-zero variance, and roughness `0.35`/`1.0` should reproduce the measured variance ordering.
4. Only after those gates, compare one bounded adaptive policy against fixed temporal accumulation using paired 64/256-frame measurements.

## Acceptance Boundaries

### May claim

- measured variance/confidence behavior for named signals, ROIs, frame windows, and deterministic sequences;
- mean preservation and variance changes for a bounded default-off experiment;
- whether an adaptive policy improves the evaluated Lit conditions.

### Must not claim yet

- production denoiser readiness or scene generalization;
- physical correctness from a current-estimator mean baseline;
- universal temporal stability, unbiasedness, Path Tracing equivalence, or DLSS Ray Reconstruction equivalence;
- promotion of stochastic sampling, temporal history, or spatial filtering to production defaults.

## Plan-Size Review

The retained roadmap is not reduced, but implementation remains staged. This branch starts with weighted-signal and moments contracts plus diagnostics. A new spatial filter, large RenderGraph refactor, and dynamic object-motion redesign are not authorized by this first work unit and require evidence from the diagnostic gates.

## 2026-08-24: Producer/consumer transition audit

- `ReflectionEvaluatePass` produces unweighted `ReflectionEvaluatedRadiance` and weighted `ReflectionSpecularEstimate` together. The weighted target uses the same incident sample and includes visible-surface Fresnel, GGX distribution/geometry, cosine, and directional-PDF compensation.
- Current `TemporalReflectionPass` reads only unweighted Evaluated Radiance. Current `LightPass` consumes its resolved form and applies distance fade, `(1 - visible roughness)`, user intensity, and `FresnelSchlickRoughness` before adding it to a color that already includes deterministic Specular IBL.
- A weighted transition must not reuse the latter visible-roughness or Fresnel multipliers. It must also avoid adding the environment miss estimate on top of deterministic Specular IBL.
- Decision: the future default-off weighted path will blend from deterministic `iblSpecular` to `ReflectionResolvedSpecularEstimate`. User intensity and retained hit-distance policy control that blend. A finite-hit distance may fade back to IBL; environment miss remains eligible for full estimator replacement.
- Defined a candidate two-channel luminance moments contract. First and second moments share resolved-estimate reprojection, acceptance, history weight, reset, and ping-pong ownership. Rejected history initializes from the current sample. Variance is `max(M2 - M1 * M1, 0)` and is not packed into radiance alpha.

No runtime path or resource was changed in this audit. The next work unit is moments format/range measurement and diagnostic exposure before adaptive policy implementation.

## 2026-08-25: Moments range and precision audit

Existing controlled-scene weighted-estimator reports were inspected without rerunning the renderer:

| Condition | Window | Maximum luminance | Maximum squared luminance |
| --- | ---: | ---: | ---: |
| roughness `0.0`, metallic | 256 frames | `0.307697` | `0.094677` |
| roughness `0.35`, metallic | 1024 frames | `7.82340` | `61.2056` |
| roughness `1.0`, metallic | 1024 frames | `5.29550` | `28.0423` |

All measured values fit within FP16 range, but range is not the deciding constraint. Computing `M2 - M1^2` is cancellation-sensitive, and the deterministic roughness `0.0` mirror control should remain near zero. Initial diagnostic history will therefore use `R32G32_FLOAT` (`.x = M1`, `.y = M2`). FP16 remains a later memory/bandwidth optimization gated by a paired precision comparison.

No code or shader changed in this work unit, so no build was required. Next: add the separate ping-pong moments resource and debug exposure without connecting the weighted result to LightPass.

## 2026-08-25: Resolved weighted estimate and moments diagnostics

- Extended `TemporalReflectionPass` with two diagnostic MRT outputs: ping-pong `ReflectionResolvedSpecularEstimate` in `R16G16B16A16_FLOAT` and ping-pong `ReflectionSpecularMoments` in `R32G32_FLOAT`.
- Both outputs use the existing motion-vector reprojection, bounds check, depth/normal acceptance, history reset, accepted history weight, and history role exchange. Rejected history initializes the resolved estimate and moments from the current weighted sample.
- Added `Resolved Specular` and `Specular Variance` UI debug views plus `resolved-specular-estimate` and `specular-variance` capture selectors.
- The variance view computes `max(M2 - M1^2, 0)` and applies `v / (1 + v)` for display only. Stored moments remain linear and unmapped.
- LightPass remains bound to the legacy unweighted `ReflectionResolvedRadiance`; no production composition or default changed.

Validation:

- Debug x64 rebuilt C++ and all affected HLSL with zero errors and the existing duplicate-vcpkg-import warning.
- A 64-frame roughness `0.35` runtime smoke completed with zero D3D12 errors and the same three known committed-buffer warnings.
- A 120-frame warm-up `Specular Variance` capture completed with zero D3D12 errors. Visual inspection reproduced the expected ordering: rough spheres show strong variance while the mirror end is nearly black.

Generated JSON, PNG, and log outputs remain untracked. This establishes diagnostic observability, not an adaptive filter result.

## 2026-08-25: Linear-HDR moments report and roughness gate

- Extended the HDR diagnostic readback and schema from version 3 to version 4. Reports now include `ReflectionResolvedSpecularEstimate` temporal statistics and `ReflectionSpecularMoments` summaries without converting the stored linear values through the debug display mapping.
- Moments summaries contain ROI/frame-window mean `M1`, mean `M2`, mean `max(M2 - M1^2, 0)`, and maximum estimated variance. This is temporal-estimator metadata, not physical ground truth.
- Ran deterministic 64- and 256-frame measurements after a 32-frame warm-up with stochastic sampling enabled, temporal weight `0.9`, a fixed camera/scene, and 48x48 metallic-sphere ROIs. Independent 64- and 256-frame processes reproduced identical first-64-frame resolved-estimate and moments samples in all three ROIs.

| Roughness | 64-frame mean estimated variance | 256-frame mean estimated variance | 256-frame resolved temporal variance | 256-frame resolved frame-difference p99 |
| ---: | ---: | ---: | ---: | ---: |
| `1.0` | `0.0781654` | `0.0814923` | `0.00418381` | `0.117602` |
| `0.35` | `0.00605023` | `0.00611414` | `0.000318847` | `0.0111988` |
| `0.0` | `1.00777e-10` | `1.00777e-10` | approximately `0` | `0` |

The observability gate passes: the mirror control remains effectively zero, roughness `0.35` reports nonzero variance, and roughness `1.0` reports the highest ROI-average variance. Debug x64 completed with zero errors and the existing duplicate-vcpkg-import warning. All six runtime measurements completed with zero D3D12 errors and the same three known committed-buffer warnings.

These results validate the diagnostic ordering and deterministic measurement contract only. They do not establish a production filtering policy, scene generalization, physical correctness, or an optimal variance threshold. The next bounded work unit may compare one default-off adaptive policy against fixed temporal accumulation using the same paired 64/256-frame conditions.

## 2026-08-25: Bounded variance-guided temporal experiment

Implemented one default-off weighted-estimator-only policy. For accepted history it computes prior relative variance as `saturate(max(M2 - M1^2, 0) / max(M2, 1e-6))` and interpolates the effective weighted-estimator history weight from the configured base weight toward `0.98`. The resolved weighted estimate and moments share this effective weight. Legacy unweighted resolved radiance and LightPass remain unchanged. The policy is exposed through `Variance-Guided Temporal` and `-ReflectionVarianceGuidedTemporal`.

Paired A/B used stochastic sampling, base weight `0.9`, 32 warm-up frames, fixed camera/scene/ROI, and identical current sample sequences. Values below compare adaptive B with fixed-weight A.

| Roughness | Window | Mean change | Temporal variance change | Frame-difference p99 change | Result |
| ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | 64 | `+11.82%` | `-75.40%` | `-70.66%` | Reject: short-window mean shift |
| `0.35` | 64 | `+2.13%` | `+25.99%` | `-16.99%` | Reject: variance increase |
| `0.0` | 64 | `0%` | `0%` | `0%` | Mirror control preserved |
| `1.0` | 256 | `+7.10%` | `-74.33%` | `-73.60%` | Reject: mean shift |
| `0.35` | 256 | `+3.27%` | `+78.92%` | `-18.07%` | Reject: variance increase and mean shift |
| `0.0` | 256 | `0%` | `0%` | `0%` | Mirror control preserved |

The initial formula is not promoted. It demonstrates that stronger history can reduce rough-surface frame differences, but variance magnitude alone is insufficient to select an effective history weight: convergence state and weight stability also matter. The implementation remains default-off as a bounded diagnostic experiment. All paired runs had identical current samples, zero D3D12 errors, and the existing three committed-buffer warnings per process.

Next: expose or record the effective weighted history weight, then evaluate a bounded confidence/clamping rule before connecting weighted history to LightPass. This is a policy refinement gate, not a reduction of the Hybrid Reflection roadmap.

## 2026-08-25: Effective-weight diagnostics and bounded confidence policy

- Added schema version 8 policy-weight diagnostics. The HDR capture now also reads visible PBR parameters, and each frame plus the aggregate report records mean, standard deviation, minimum, p95, p99, and maximum policy-selected weight.
- The reported value predicts the next accepted same-pixel history weight from stored moments and visible roughness. It intentionally does not reproduce motion reprojection, so its interpretation is limited to the fixed-camera ROI workflow.
- Weight diagnostics explained the rejected continuous formula: roughness `1.0` averaged `0.9665` with p99 `0.9786`, while roughness `0.35` averaged only `0.9139` but contained sparse p99 `0.9719` outliers. Mirror roughness `0.0` stayed at the `0.9` base.
- A threshold-only v2 (`relative variance >= 0.5` selects `0.94`) preserved the roughness `1.0` benefit but sparse roughness `0.35` switching still increased 64-frame temporal variance by `28.51%`.
- The bounded v3 policy adds visible roughness `>= 0.75`. Only when both confidence conditions pass does it select `max(base_weight, 0.94)`; otherwise it keeps the base weight.

Paired v3 results with base weight `0.9`, 32 warm-up frames, fixed 48x48 metallic ROIs, and identical current sample sequences:

| Roughness | Window | Mean change | Temporal variance change | Frame-difference p99 change | Policy-weight mean | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | 64 | `-0.044%` | `-49.46%` | `-39.91%` | `0.93996` | Pass |
| `0.35` | 64 | `0%` | `0%` | `0%` | `0.9` | Pass, unchanged control |
| `0.0` | 64 | `0%` | `0%` | `0%` | `0.9` | Pass, mirror control |
| `1.0` | 256 | `-0.323%` | `-43.90%` | `-39.88%` | `0.93997` | Pass |
| `0.35` | 256 | `0%` | `0%` | `0%` | `0.9` | Pass, unchanged control |
| `0.0` | 256 | `0%` | `0%` | `0%` | `0.9` | Pass, mirror control |

Debug x64 and affected HLSL completed with zero errors and the existing duplicate-vcpkg-import warning. All v3 runtime runs completed with zero D3D12 errors and the existing three committed-buffer warnings per process.

Scoped claim: in the evaluated controlled high-roughness metallic ROI, the default-off v3 policy reduced linear-HDR temporal variance and frame-difference p99 while keeping the 256-frame mean change below 1%. It does not establish behavior for intermediate roughness above the gate, dielectric surfaces, textured production assets, motion, or Lit composition. Those conditions form the next generalization and composition gates.

## 2026-08-25: Controlled material generalization gate

The Estimator Test scene contains roughness values `0.0`, `0.05`, `0.15`, `0.35`, `0.6`, and `1.0` in metallic and dielectric rows. Because there is no exact `0.75` sphere, the gate used roughness `0.6` as the below-threshold control and roughness `1.0` dielectric as the cross-material active condition.

Paired measurements used the same base weight `0.9`, 32-frame warm-up, fixed 48x48 ROIs, and identical current samples:

| Condition | Window | Mean change | Temporal variance change | Frame-difference p99 change | Policy-weight mean | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| roughness `1.0`, dielectric | 64 | `-0.135%` | `-49.56%` | `-39.94%` | `0.93994` | Pass |
| roughness `0.6`, metallic | 64 | `0%` | `0%` | `0%` | `0.9` | Pass, below-gate control |
| roughness `0.6`, dielectric | 64 | `0%` | `0%` | `0%` | `0.9` | Pass, below-gate control |
| roughness `1.0`, dielectric | 256 | `-0.181%` | `-43.75%` | `-39.98%` | `0.93998` | Pass |
| roughness `0.6`, metallic | 256 | `0%` | `0%` | `0%` | `0.9` | Pass, below-gate control |
| roughness `0.6`, dielectric | 256 | `0%` | `0%` | `0%` | `0.9` | Pass, below-gate control |

All processes completed with zero D3D12 errors and the existing three committed-buffer warnings each. This expands the scoped claim across metallic and dielectric high-roughness controls and confirms the explicit below-threshold behavior. It does not yet establish performance on textured production assets or motion.

Next: run the same fixed/bounded comparison on the two known DamagedHelmet material-region ROIs, then use scoped subjective captures before any Lit composition change.

## 2026-08-25: DamagedHelmet development gate

The fixed/bounded v3 comparison ran for 64 frames after a 32-frame warm-up in the two established DamagedHelmet ROIs. Current sample sequences matched.

| ROI | Center roughness / metallic | Mean change | Temporal variance change | Frame-difference p99 change | Policy-weight mean | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `rearward_surface` | `0.133 / 0.953` | `-0.012%` | `-0.12%` | `-0.02%` | `0.90258` | Fail: negligible practical effect |
| `underside_pipes` | `0.216 / 0.992` | `-0.037%` | `-0.33%` | `-0.55%` | `0.90339` | Fail: negligible practical effect |

The controlled high-roughness policy does not generalize to the known production-asset noise regions because most of these textured ROIs are below the explicit roughness `0.75` gate. The 256-frame standard run is not performed for this formula: the development gate already shows that the policy is effectively inactive in the target regions. This is not a production regression because the experiment remains default-off.

Do not respond by merely lowering the roughness threshold. The threshold-only v2 already showed that sparse per-pixel switching can increase variance at roughness `0.35`. The next contract step is a persistent confidence/history signal that can stabilize policy activation over time. This is the previously reserved confidence extension, now justified by measured controlled-scene and DamagedHelmet evidence.

## 2026-08-26: Persistent confidence contract

- Selected a separate ping-pong `ReflectionSpecularConfidence` resource in `R16_FLOAT`. Confidence remains scalar metadata and is not packed into radiance alpha or the two-channel moments resource.
- Confidence shares weighted-estimator reprojection, depth/normal acceptance, reset, ownership, and post-submit role exchange. Rejected or reset history initializes confidence to zero.
- Accepted history converts prior relative variance into a binary indicator at `0.5`, then applies a fixed `0.9` confidence-history weight. This requires persistent evidence before activation and provides symmetric decay when evidence disappears.
- Updated confidence maps through `smoothstep(0.5, 0.9, confidence)` between the configured base history weight and `max(base, 0.94)`. The resolved weighted estimate and moments share that effective weight. Legacy unweighted resolved radiance keeps the configured base weight.
- The roughness `0.75` gate is removed from this candidate. The purpose of persistent confidence is to admit lower-roughness production-asset noise while preventing the sparse instantaneous switching measured in v2.

This work unit fixes semantics only. Next: implement the resource, MRT output, debug/report observability, then validate confidence rise/decay before paired quality claims.

## 2026-08-26: Persistent confidence implementation and contract gate

- Added ping-pong `ReflectionSpecularConfidence` as `R16_FLOAT`, including transient registration, RTV/SRV allocation, RenderGraph ownership, history binding, reset/release handling, and post-submit role exchange through the existing reflection history state.
- Extended Temporal Reflection to six MRTs. The shader writes confidence target 5, reads reprojected confidence from space 17, removes the roughness gate, and applies the documented persistent-confidence update and smooth bounded weight.
- Expanded the generic fullscreen pipeline definition from four to five additional render targets. No other fullscreen pass adds a target.
- Added `Specular Confidence` UI debug view. White indicates sustained high-variance evidence; rejected/reset history is black.
- Extended HDR readback for `R16_FLOAT` and schema version 9. Reports contain current motion-reprojected confidence and applied effective-weight distributions rather than the schema v8 static same-pixel prediction.

The controlled 64-frame contract gate used a 32-frame warm-up, stochastic sampling, base weight `0.9`, and fixed 48x48 metallic ROIs:

| Roughness | Confidence mean | Confidence p95 | Last-frame mean | Effective-weight mean | Result |
| ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | `0.9828` | `0.9956` | `0.9944` | `0.93984` | Pass: persistent activation |
| `0.35` | `0.0133` | `0` | `0.0201` | `0.90043` | Pass: sparse spikes do not broadly activate |
| `0.0` | `0` | `0` | `0` | `0.9` | Pass: mirror control |

Debug x64 and affected HLSL completed with zero errors and the existing duplicate-vcpkg-import warning. Runtime reports completed with zero D3D12 errors and the existing three committed-buffer warnings per process. This validates resource ownership and steady confidence separation, not yet paired image-quality improvement or confidence decay under changing input.

Next: run fixed/confidence-guided paired 64-frame quality gates across controlled roughness conditions, followed by explicit confidence decay and DamagedHelmet development tests.

## 2026-08-26: Persistent confidence paired quality gate

Fixed-weight and confidence-guided runs used identical sampling-frame sequences, a 32-frame warm-up, base history weight `0.9`, stochastic sampling, and the same 48x48 metallic ROIs. The compared signal is `ReflectionResolvedSpecularEstimate`; legacy unweighted resolved radiance is intentionally unchanged.

| Roughness | Frames | Mean change | Temporal-variance change | Frame-difference p99 change | Confidence mean | Effective-weight mean | Result |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | `64` | `+0.0886%` | `-49.62%` | `-39.91%` | high | near `0.94` | Pass: targeted variance reduction |
| `0.35` | `64` | `+0.2590%` | `-2.38%` | `-2.20%` | low | near `0.9` | Pass: bounded response |
| `0.0` | `64` | `0%` | `0%` | not applicable | `0` | `0.9` | Pass: unchanged mirror control |
| `1.0` | `256` | `-0.2873%` | `-43.95%` | `-39.87%` | `0.99170` | `0.93993` | Pass: standard-frame confirmation |
| `0.35` | `256` | `+0.3373%` | `+2.10%` | `-2.45%` | `0.02141` | `0.90072` | Pass with limitation: small variance regression |
| `0.0` | `256` | `0%` | `0%` | not applicable | `0` | `0.9` | Pass: unchanged mirror control |

All paired sample sequences matched. The six 256-frame processes completed with zero D3D12 errors and the existing three committed-buffer warnings per process. The result supports selective persistent activation in the controlled scene. It does not establish confidence decay, DamagedHelmet effectiveness, Lit quality, estimator correctness, or production readiness. The roughness `0.35` variance increase is retained as a limitation rather than hidden by the frame-difference improvement.

Next: validate explicit confidence decay, then run the established DamagedHelmet ROIs as the production-asset development gate.

## 2026-08-26: DamagedHelmet persistent-confidence gate

The established textured-asset ROIs used paired fixed/confidence-guided processes with matching sampling-frame sequences, 32 warm-up frames, stochastic sampling, and base weight `0.9`.

| ROI | Frames | Mean change | Temporal-variance change | Frame-difference p99 change | Confidence mean | Effective-weight mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | `64` | `-0.0095%` | `-5.03%` | `-1.28%` | `0.06214` | `0.90189` |
| `underside_pipes` | `64` | `-0.0130%` | `-20.60%` | `-3.20%` | `0.10536` | `0.90322` |
| `rearward_surface` | `256` | `-0.0078%` | `-4.10%` | `-1.34%` | `0.06393` | `0.90199` |
| `underside_pipes` | `256` | `-0.0207%` | `-17.78%` | `-3.35%` | `0.10948` | `0.90339` |

All eight processes completed with zero D3D12 errors and the existing two committed-buffer warnings per process. Persistent confidence therefore generalizes beyond the removed roughness gate to both known textured-asset noise regions, but only with a bounded effect: the rearward ROI improves by about four percent at the standard frame count, and neither ROI approaches the controlled roughness `1.0` activation level. This is a linear-HDR diagnostic result for the weighted estimator, not a claim about Lit perceptual quality or production denoiser readiness.

The diagnostic application writes its complete report but does not automatically exit afterward; the runner now waits for the report and stops only the process it launched. This automation behavior is recorded separately from the rendering result.

Next: run an explicit confidence decay/reset transition gate before deciding whether this policy is ready for Lit subjective comparison.

## 2026-08-26: Confidence decay and reset transition gates

Added diagnostic-only measurement transitions. `-ReflectionConfidenceForceStableAfterMeasurementFrames N` forces the confidence variance indicator to zero after `N` captured frames without changing radiance, sampling, history validity, or the acceptance path. `-ReflectionHistoryResetAfterMeasurementFrames N` explicitly invalidates reflection history after `N` captured frames. Schema version 10 records both transition contracts. Neither option affects normal rendering defaults.

The decay gate used the controlled roughness `1.0` metallic ROI, 32 warm-up frames, and 96 measured frames. Stable evidence began after measured frame 31. Confidence fell from `0.993395` at frame 31 to `0.893947` at frame 32 and `0.473901` at frame 38. Effective weight returned to the exact base `0.9` at frame 38, six frames after the transition, while temporal acceptance remained `1.0`. Confidence reached `0.001147` by frame 95. This isolates accepted-history decay from reset or rejection.

The reset gate used the same controlled ROI and reset after 16 measured frames. Confidence changed from `0.978657` at frame 15 to `0` at frame 16; effective weight returned from `0.939883` to `0.9`, and acceptance was zero for the reset frame. The next frame resumed accepted history at zero confidence, followed by reconstruction to confidence `0.904974` at frame 47. This validates reset ownership independently of natural decay.

Debug x64 and affected HLSL built with zero errors and the existing duplicate-vcpkg-import warning. Both runtime gates completed with zero D3D12 errors and the existing three committed-buffer warnings. The diagnostic executable still required the runner to stop its launched process after report completion.

The persistent-confidence lifecycle gate is complete. Next: prepare a bounded Lit subjective comparison that checks static noise, motion response, and whether the measured weighted-estimator improvement is perceptible in final composition.

## 2026-08-26: Lit composition gate correction

The first fixed/confidence Lit capture attempt produced byte-identical PNGs at all four matched checkpoints: static, mid motion, direction reversal, and settling. This is expected from the then-current wiring, not subjective evidence of equal quality. Persistent confidence affected only `ReflectionResolvedSpecularEstimate`, while LightPass consumed the fixed-weight legacy `ReflectionResolvedRadiance`.

The default-off policy is therefore extended to use the same effective history weight for `ReflectionResolvedRadiance`, `ReflectionResolvedSpecularEstimate`, and `ReflectionSpecularMoments`. This does not feed the BRDF-weighted diagnostic estimator into LightPass and does not move Fresnel or final contribution weighting out of LightPass. It only lets confidence control the accumulation rate of the existing unweighted radiance boundary. With the policy disabled, the base-weight path remains unchanged.

Next: rebuild, repeat controlled and DamagedHelmet numerical regression for unweighted resolved radiance, then regenerate the Lit suite. The byte-identical first captures are not accepted as a subjective gate result.

The corrected 64-frame paired regression used matching sample sequences and passed:

| ROI | Unweighted mean change | Unweighted temporal-variance change | Unweighted frame-difference p99 change | Control result |
| --- | ---: | ---: | ---: | --- |
| controlled roughness `1.0` | `+0.2702%` | `-49.52%` | `-39.69%` | strong activation |
| controlled roughness `0.0` | `0%` | `0%` | `0%` | unchanged mirror control |
| DamagedHelmet `rearward_surface` | `+0.0020%` | `-6.76%` | `-4.21%` | bounded improvement |
| DamagedHelmet `underside_pipes` | `+0.0233%` | `-27.31%` | `-10.24%` | bounded improvement |

All eight final processes reported zero D3D12 errors; controlled runs retained three known committed-buffer warnings and DamagedHelmet runs retained two. The first pipes B attempt was stopped after the runner mistook a newly created zero-byte report for completion. It was rerun successfully after completion detection required valid JSON with all 64 frames. This was an automation race, not a rendering failure.

Added a four-checkpoint enlarged DamagedHelmet Lit capture plan and bilingual suite covering static appearance, mid motion, direction reversal, and settling. After the composition correction, all four B captures differ from their matched A captures, while both capture runs report zero D3D12 errors and two known warnings. This establishes that the policy now reaches Lit composition; subjective quality is not yet claimed.

User review found that the initial `0.5` camera-distance scale kept the underside pipes visible but clipped the established rearward-surface region near the top of the image. The Lit suite now uses scale `0.6`, approximately 1.67x the original linear size. The recaptured frame keeps the smooth crown panel, its adjacent rearward surface, and the underside pipes visible together. The static criterion now names both regions instead of relying on the ambiguous phrase "known noise regions."

The validation runner also no longer assumes that the capture process exits after writing its plan. For each variant it waits until every planned output is non-empty and has a write time after that variant started, then stops only the process it launched. This resolves the observed post-capture process timeout without treating older capture files as completion.
