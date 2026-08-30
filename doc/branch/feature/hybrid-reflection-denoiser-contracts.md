# Hybrid Reflection Denoiser Contracts

## Scope

This document fixes the signal, metadata, ownership, reset, and pass-boundary contracts required to add denoising without changing Hybrid Reflection semantics. It separates contracts already implemented by `TemporalReflectionPass` from requirements for a future edge-aware spatial pass. It does not claim production denoiser readiness and does not connect DLSS Ray Reconstruction, Streamline, or PathTracing.

## Signal Flow

```text
HybridReflectionPass
    -> ReflectionEvaluatePass
    -> TemporalReflectionPass
    -> default-off EdgeAwareSpatialReflectionPass
    -> LightPass
```

The implementation has a dedicated default-off spatial-pass output. `LightPass` consumes `ReflectionResolvedRadiance` while the filter is disabled and `ReflectionDenoisedRadiance` while it is enabled. `ReflectionResolvedRadiance` remains the temporal-history boundary in both cases.

All three radiance boundaries are linear HDR and retain unweighted one-bounce-radiance semantics:

| Boundary | Meaning | May contain temporal processing | May contain spatial processing | Final visible-surface weighting |
|----------|---------|---------------------------------|--------------------------------|---------------------------------|
| `ReflectionEvaluatedRadiance` | Current-frame evaluated signal | No | No | Excluded |
| `ReflectionResolvedRadiance` | Temporally resolved RGB signal; alpha is the temporal-validity diagnostic code | Yes | No | Excluded |
| `ReflectionDenoisedRadiance` | Spatially processed resolved signal | Yes, inherited from input | Yes | Excluded |

`LightPass` remains the sole owner of the legacy distance, visible roughness, intensity, and visible-surface Fresnel weighting. A denoiser must not bake those terms into any unweighted radiance boundary.

The experimental weighted `ReflectionSpecularEstimate` family is a separate estimator contract. A pass must not mix it with the unweighted radiance family or bind it where unweighted radiance is expected.

## Temporal Input and Output Contract

The implemented `TemporalReflectionPass` consumes:

- current `ReflectionEvaluatedRadiance`;
- current `ReflectionSpecularEstimate` for the separate weighted diagnostic path;
- current visible depth, world normal, material roughness/metallic, and motion vector;
- previous resolved radiance, resolved weighted estimate, moments, confidence, depth, and normal from one reflection-owned history generation.

It produces one coherent generation containing:

- `ReflectionResolvedRadiance`;
- `ReflectionResolvedSpecularEstimate`;
- `ReflectionSpecularMoments`;
- `ReflectionSpecularConfidence`;
- `ReflectionHistoryDepth`;
- `ReflectionHistoryNormal`.

All outputs use the same history-valid decision, reprojection coordinate, reset event, physical write slot, and post-submit role exchange. A partially updated generation must never become history.

## Future Spatial Input and Output Contract

The default-off `EdgeAwareSpatialReflectionPass` consumes the current resolved generation and current-frame correspondence features. Its input set is:

| Input | Purpose | Contract restriction |
|-------|---------|----------------------|
| `ReflectionResolvedRadiance` | Unweighted signal to filter | Preserve linear-HDR, unweighted semantics |
| visible depth | Stop cross-surface leakage | Compare in a documented depth space; do not treat device-depth deltas as view-independent |
| visible world normal | Preserve geometric and normal-mapped edges | Normalize before comparison |
| visible roughness | Control lobe-compatible neighborhood support | It is a weight/gate, not proof of correspondence |
| `ReflectionRayHit.x/y` | Hit distance and hit/miss class | Do not mix finite-hit and miss samples without an explicit policy |
| encoded hit normal | Optional reflected-surface discontinuity evidence | Decode and normalize before comparison |
| variance/moments | Estimate local signal instability | Metadata is not radiance |
| confidence | Modulate filter strength conservatively | Confidence is evidence persistence, not correctness probability |

The output is `ReflectionDenoisedRadiance`. Its RGB preserves the linear-HDR unweighted signal. Its implemented alpha is `1` and does not copy or redefine the input temporal-validity code. The output uses a distinct resource rather than overwriting temporal history in place. This preserves A/B observability, avoids read/write hazards, and prevents spatial output from silently becoming next-frame temporal history.

Temporal history owns `ReflectionResolvedRadiance`, not `ReflectionDenoisedRadiance`. Feeding spatial output back into temporal history remains a separate policy change requiring stability, bias, and reset validation.

## History Ownership

Reflection history is owned by the reflection pipeline, not by swap-chain indices, the Temporal Upscaler, DLSS, or a future spatial pass. One logical history state selects a read generation and the opposite write generation. Role exchange occurs only after successful submission of every output in the generation.

The spatial pass is stateless. Its enable-state change does not reset reflection history. If a later spatial algorithm introduces persistent state, that state must have an explicit owner and invalidation contract; it must not reuse temporal ping-pong slots implicitly.

## Reset and Rejection

A hard reset invalidates radiance, weighted-estimator, moments, confidence, depth, and normal histories together. The complete hard-reset event list remains defined by [Hybrid Reflection Contracts](hybrid-reflection-contracts.md#history-reset). On the first successful temporal frame after reset:

- old history is not sampled;
- resolved outputs are initialized from current inputs;
- moments are initialized from the current weighted sample;
- confidence is initialized to zero;
- the produced generation becomes valid only after successful submission.

Rejection and reset have different meanings. Reset invalidates the whole history generation. Rejection invalidates history for one reprojected pixel and initializes that pixel from current data.

The implemented per-pixel acceptance evidence is bounds, previous depth, and previous visible normal. Motion vectors select the previous pixel but do not by themselves establish validity. Visible roughness, hit/miss class, hit distance, and hit normal are future reflection-specific rejection candidates. They must be added only with a named debug reason and a measurable failure they address.

## Variance and Confidence Semantics

`ReflectionSpecularMoments` stores temporally resolved first and second luminance moments of the weighted estimator. Derived variance is non-negative signal dispersion under the current estimator and sample sequence. It is not estimator bias, physical error, or distance from ground truth.

`ReflectionSpecularConfidence` stores persistent high-variance evidence in `[0, 1]`. It is not a probability that history is correct, not a history-valid flag, and not a material classification. Reset or rejection writes zero. Accepted history may raise or decay it according to the documented experimental policy.

A spatial filter may use moments or confidence to reduce strength in stable regions or increase support in persistently noisy regions, but it must preserve the following:

- invalid history cannot be made valid by high confidence;
- confidence cannot override depth/normal discontinuities;
- filter strength must be bounded and observable;
- disabling the filter must bypass the spatial pass without changing temporal results;
- variance reduction alone does not establish mean preservation or estimator correctness.

## Required Observability and Gates

The first edge-aware spatial implementation must remain default-off and expose enough information to distinguish signal improvement from blur or bias. At minimum, validation must compare matched filter-off/on runs with the same camera, animation, reset frame, sample-index reset, warm-up range, and measurement range.

Required evidence is:

- linear-HDR temporal variance and frame-difference statistics;
- long-run ROI mean change;
- edge leakage across depth, normal, roughness, and hit/miss boundaries;
- temporal response during motion, reversal, and settling;
- Lit-view detail and brightness preservation;
- D3D12 Debug Layer and shader/build results;
- multiple controlled roughness/metallic cases plus a textured production asset.

Passing these gates supports only the scoped filter and evaluated conditions. Production default, physical correctness, PathTracing agreement, and DLSS RR compatibility remain separate claims.

## Out of Scope

- implementing the edge-aware spatial pass;
- feeding spatial output back into temporal history;
- production-default selection;
- adaptive sampling or multi-bounce transport;
- PathTracing reference implementation;
- DLSS Ray Reconstruction or Streamline backend integration.
