# features/hybrid-reflection-estimator-correctness

## Objective

Audit and define the estimator behind stochastic Hybrid Reflection before adding PDF or BRDF-throughput compensation. Reuse the Phase 1 linear-HDR diagnostics to distinguish variance, signal preservation, and estimator correctness.

This phase must not treat the existing High-SPP Current-Estimator Mean Baseline as a physical ground truth.

## Initial Audit

### Sampling

- `ReflectionSampling.hlsli` draws a deterministic per-pixel/per-frame 2D sample.
- It samples a GGX NDF half-vector with `alpha = roughness * roughness` and reflects the visible-surface view direction around that half-vector.
- It is NDF sampling, not GGX VNDF sampling.
- A below-surface sampled direction falls back to the deterministic mirror direction. The fallback changes the sampling distribution and currently has no explicit probability accounting.
- Roughness at or below `0.001` uses the mirror direction.

### Signal evaluation

- `HybridReflectionPass` traces the sampled direction and stores raw hit/material payloads.
- `ReflectionEvaluatePass` reconstructs the same sampled direction from pixel/frame inputs.
- A miss samples the sharp environment mip when stochastic sampling is enabled.
- A hit evaluates outgoing radiance at the hit surface toward the visible surface, including direct light, diffuse/specular IBL, and emission.
- The evaluated result remains unweighted with respect to the visible surface. Distance, visible roughness, intensity, and Fresnel contribution remain owned by `LightPass`.

### Missing Monte Carlo estimator terms

- no explicit half-vector PDF;
- no conversion from half-vector PDF to reflection-direction PDF;
- no visible-surface Cook-Torrance BRDF factor in the sampled-radiance signal;
- no `f_r * L_i * (N dot L) / p(L)` throughput;
- no accounting for mirror fallback probability;
- no estimator confidence or sample PDF payload.

## Contract Decision Required Before Implementation

The current path is best described as a stochastic rough-direction approximation of incident one-bounce radiance, followed by heuristic visible-surface weighting in `LightPass`. It is not currently a Monte Carlo estimator of the visible-surface BRDF integral.

Adding PDF compensation only inside `ReflectionEvaluatePass` would be unsafe because visible-surface Fresnel and contribution weighting are deliberately deferred to `LightPass`. A correct Monte Carlo estimator may require moving or redefining part of that ownership. The phase must first choose and document one target:

1. Preserve the current unweighted-radiance contract and treat stochastic sampling as a bounded approximation, or
2. Introduce an explicit BRDF-integral estimator contract with directional PDF and throughput, then revise final contribution ownership to avoid double weighting.

No production-default change should occur until this decision is made and Phase 1 diagnostics are rerun.

## Planned Gates

1. Write the estimator target and ownership decision.
2. Derive the current GGX NDF half-vector and directional PDFs, including invalid/fallback behavior.
3. Define mirror-limit, environment-miss, and geometry-hit semantics under the selected estimator.
4. Add only the minimal payload/debug data needed to inspect PDF and throughput.
5. Compare roughness conditions against deterministic IBL and the High-SPP Current-Estimator Mean Baseline, without calling either a physical ground truth.
6. Rerun Phase 1 paired HDR diagnostics after any estimator change.

## Controlled Evaluation Scene

### 2026-08-22: Initial scene implementation

- Added `Hybrid Reflection Estimator Test` with no external asset dependency.
- The scene contains twelve identical spheres in a fixed two-row grid.
- Columns use visible roughness values `0.0`, `0.05`, `0.15`, `0.35`, `0.6`, and `1.0` from left to right.
- The upper row is metallic `1.0`; the lower row is dielectric metallic `0.0`.
- All sphere materials use the same neutral albedo, no normal map, no emission, and full ambient occlusion.
- A rough dark floor provides a stable geometry and depth context.
- A narrow off-axis emissive target provides a high-radiance geometry-hit candidate without covering the sphere grid.
- Camera position, gaze, FOV, near plane, and far plane are fixed in scene code. Animation is absent.
- Debug x64/HLSL build succeeded with the existing duplicate vcpkg import warning only.

The scene is a measurement instrument, not a visual showcase. Fixed 1920x1080 ROIs and proof that the emissive target produces the intended hit/miss coverage remain pending visual validation; coordinates must not be guessed from code alone.

### 2026-08-23: Base scene visual validation

User visual validation passed all five base-scene checks:

- all twelve spheres are visible;
- the roughness progression is distinguishable;
- the metallic and dielectric rows are distinguishable;
- the emissive target does not obstruct the sphere grid;
- the floor and camera framing are acceptable.

This closes the base composition/framing gate. ReflectionRayHit and Evaluated Radiance hit/miss coverage, followed by fixed 1920x1080 ROI selection, remain pending.

## Out of Scope

- production temporal/spatial denoiser;
- DLSS RR backend integration;
- Path Tracing pass;
- broad RenderGraph refactor;
- promotion of the default-off Surface Variance Filter.
