# Hybrid Reflection Production Quality Gates

## Purpose

This document defines the evidence required to change a Hybrid Reflection feature from a default-off diagnostic experiment into a production candidate. It does not promote any current experiment by itself.

The current default path remains the protected baseline: Hybrid Reflection is enabled, while stochastic rough sampling is disabled, temporal history weight is `0.0`, and rejected-pixel, edge-aware spatial, spatiotemporal spatial-policy, and variance-guided temporal experiments are disabled.

## Decision Classes

| Class | Meaning | Default-state rule |
|---|---|---|
| Production baseline | Existing behavior that must not regress | May remain enabled by default |
| Production candidate | Passed every required gate for its declared scene and hardware scope | Default promotion requires a separate explicit decision |
| Diagnostic experiment | Useful for measurement, comparison, or bounded investigation | Must remain default-off |
| Rejected experiment | Failed its intended quality claim or introduces an unexplained regression | Must remain default-off; retain only when it still has diagnostic value |

Passing a scoped test is not equivalent to production promotion. `PASS WITH LIMITATION` records useful bounded evidence but cannot satisfy a production gate by itself. `NOT CLAIMED` identifies conclusions that the evidence cannot support.

## Evaluation Profiles

| Profile | Stochastic sampling | Temporal weight | Edge-aware spatial | Bounded spatial policy | Purpose |
|---|---:|---:|---:|---:|---|
| Baseline | Off | `0.0` | Off | Off | Protected default and regression reference |
| Temporal candidate | On | `0.9` | Off | Off | Temporal noise reduction and response validation |
| Fixed spatial diagnostic | On | `0.9` | On | Off | Existing fixed-filter comparison |
| Bounded spatial diagnostic | On | `0.9` | On | On | Confidence/variance-bounded comparison |

Variance-guided temporal remains a separate default-off diagnostic and is not silently combined with the spatial-policy comparison. Any future combined profile requires its own paired evidence.

## Required Scene Coverage

| Scene | Required evidence |
|---|---|
| `Hybrid Reflection Estimator Test` | Roughness `0.0` through `1.0`, metallic/dielectric separation, mirror bypass, mean preservation, variance, and frame-difference behavior |
| `Hybrid Reflection Spatial Filter Test` | Straight and curved material/geometric boundaries, emissive reflections, leakage, blur, and bounded-policy behavior |
| DamagedHelmet | Textured and normal-mapped production-asset behavior, established rearward-surface and underside-pipe noise regions, motion, reversal, settling, and Lit contribution |
| At least one additional glTF asset | Evidence that a decision is not specific to DamagedHelmet or the controlled procedural scenes |

The additional glTF asset must be chosen for visible Hybrid Reflection coverage, not merely because it loads successfully. If no suitable reflection is visible, the result is `UNABLE`, not `PASS`.

BoomBox is the selected additional asset because it combines metallic-roughness and emissive textures in a compact object. Its reproducible framing starts from versioned scene defaults and applies Arcball camera distance scale `0.25`. This selection establishes a review condition only; it does not count as a quality PASS until the declared Lit checks are completed.

## Acceptance Gates

| Gate | PASS | PASS WITH LIMITATION / failure condition |
|---|---|---|
| Default preservation | Disabled experiments bypass their passes or preserve baseline output; defaults remain unchanged | Any unexplained baseline difference fails |
| Linear-HDR mean | Paired long-run mean change remains within the declared threshold for every required ROI | A scoped pass cannot hide a mean shift elsewhere |
| Temporal stability | Variance and frame-difference tails improve or remain within the declared non-regression bound | Variance reduction with worse tails must be reported, not averaged away |
| Spatial integrity | No visible or measured leakage across depth, normal, roughness, hit-class, hit-distance, or hit-normal boundaries | Blur or color leakage fails even if variance improves |
| Dynamic response | Camera motion, reversal, and settling show no unacceptable lag, ghosting, or stale reflection | Debug-only delay is recorded separately from Lit impact |
| Lit quality | Brightness, reflection identity, thin detail, and emissive contribution are preserved | “No visible difference” proves non-regression only, not improvement |
| Runtime safety | Debug x64 and affected HLSL build; zero new D3D12 errors and zero new warnings | Known warnings must be enumerated |
| Performance | GPU cost and memory delta are measured on the declared hardware and resolution | Unmeasured performance blocks production promotion |
| Reproducibility | Paired runs share sample index, reset point, camera, animation, warm-up, and frame range | Mismatched sequences invalidate A/B attribution |

Numerical thresholds are recorded with each report rather than inferred after seeing the result. The 256-frame run is the standard quantitative gate; 64 frames is a development check, and 1024 frames is an additional drift/firefly audit when indicated.

The versioned ROI source is `Tests/SubjectiveValidation/HybridReflection/production-quality-gate-profiles.json`. Production-gate runs must select a profile by name through `Invoke-ProductionQualityGates.ps1`; manually copied coordinates are development probes and are not accepted as gate evidence. The runner treats paired sequence identity, unchanged resolved-radiance control variance, and the declared mean bound as invariants. It reports variance and frame-difference changes as observations rather than converting them into an automatic quality claim.

## Current Classification

| Feature | Current class | Evidence-based reason |
|---|---|---|
| Deterministic Hybrid Reflection baseline | Production baseline | Current protected default |
| Stochastic rough sampling plus temporal accumulation | Diagnostic experiment | Noise reduction is observable, but estimator correctness, performance, and broad production-scene coverage remain incomplete |
| Fixed edge-aware spatial filter | Diagnostic experiment | Scoped variance/noise-shape effects exist, but subjective benefit is weak and production generalization is not established |
| Bounded spatiotemporal spatial policy | Diagnostic experiment | Preserves paired means and reduces some frame-difference tails, but can increase temporal variance and had no perceptible A/B difference in the scoped Lit review |
| Variance-guided temporal policy | Diagnostic experiment | Bounded controlled and DamagedHelmet evidence exists, but it is not a production default and is not part of the current spatial-policy profile |

## Claim Boundary

This phase may establish that current defaults remain safe, diagnostics are reproducible, and specific experimental profiles pass or fail declared quality gates. It does not establish physical correctness, unbiased estimation, Path Tracing agreement, DLSS Ray Reconstruction readiness, or universal scene generalization.

The complete named-profile 64-frame development run passed all scheduling, resolved-control, and mean invariants. It did not establish consistent quality improvement. The 256-frame standard run reproduced that conclusion. The DamagedHelmet rearward-surface profile remained unchanged, while the underside-pipe profile kept mean difference within the `0.5%` bound (`0.426%`) but increased temporal variance by `21.44%` as frame-difference p99 decreased by `15.99%`. Controlled roughness `0.35` and `1.0` likewise reduced frame-difference tails while increasing temporal variance. This mixed result keeps the bounded policy in the diagnostic class.

