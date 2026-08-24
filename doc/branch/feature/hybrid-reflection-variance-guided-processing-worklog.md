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
