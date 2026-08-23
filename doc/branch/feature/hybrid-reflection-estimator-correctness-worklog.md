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

## GGX NDF PDF Derivation

Use `N` for the visible-surface normal, `V` for the direction from the surface toward the camera, `L` for the sampled outgoing reflection direction, and `H = normalize(V + L)` for the half-vector. The shader receives `viewDirection = -V`, samples `H`, then evaluates `L = reflect(-V, H)`.

The code sets `alpha = roughness^2`. Its inversion samples the isotropic GGX normal distribution

`D(H) = alpha^2 / (pi * ((N dot H)^2 * (alpha^2 - 1) + 1)^2)`.

Because an NDF is sampled with projected-area measure, the half-vector density is

`p_H(H) = D(H) * max(N dot H, 0)`.

For a valid reflected direction, the half-vector-to-direction Jacobian gives

`p_L(L) = p_H(H) / (4 * abs(V dot H))`.

This is the directional PDF that would accompany the current valid GGX NDF samples. It is not a VNDF PDF and does not include visibility term `G1(V)`.

### Invalid and mirror branches

- `roughness <= 0.001` is a separate deterministic mirror branch. It is a delta distribution and must not be represented as an ordinary finite directional PDF.
- For `roughness > 0.001`, samples with `N dot L <= 0` are currently replaced by the mirror direction.
- Therefore, the actual implemented distribution is a mixture: the continuous valid-direction density above plus all invalid-sample probability mass collapsed onto a mirror-direction delta.
- The collapsed probability mass depends on `N`, `V`, and roughness and is not calculated by the shader.
- Applying `f_r * L_i * (N dot L) / p_L(L)` while retaining this fallback would not define a correct estimator. The mirror delta and continuous branch require different probability accounting.

### Recommended estimator contract

For the explicit BRDF-integral path, the smallest auditable contract is:

1. Keep roughness `0`/mirror-limit evaluation as a named deterministic branch.
2. For the stochastic branch, return an invalid sample with zero contribution when `N dot L <= 0`; do not remap it to mirror and do not trace a ray.
3. Carry or reconstruct `p_L(L)` and apply the visible-surface Cook-Torrance term as `f_r * L_i * max(N dot L, 0) / p_L(L)`.
4. Treat environment miss and geometry hit as two sources of the same incident-radiance sample `L_i`; they must share sampling PDF and visible-surface throughput semantics.
5. Move visible-surface Fresnel/BRDF ownership out of the later heuristic `LightPass` weighting for this explicit estimator path, or otherwise remove duplicate factors. Preserve the current path as a comparison mode until paired HDR diagnostics pass.

Returning zero for below-surface directions preserves the original sampling distribution and makes the zero-integrand domain explicit. Resampling only valid directions would instead create a conditional distribution and require its normalization. GGX VNDF remains a later comparison candidate, not an assumption in the first correctness implementation.

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

### 2026-08-23: ReflectionRayHit coverage validation

User visual validation passed all Reflection Debug `Hit` checks:

- bright geometry-hit regions are present;
- dark environment-miss regions are present;
- hit/miss differences are distinguishable between spheres or within a sphere.

The controlled scene therefore provides both geometry-hit and environment-miss samples. Evaluated Radiance behavior and fixed ROI selection remain pending.

### 2026-08-23: Evaluated Radiance deterministic/stochastic comparison

With `Stochastic Rough Sampling` disabled, the user confirmed that roughness varies horizontally and changes the IBL appearance. Geometry reflections of the spheres, floor, and emissive target remained similarly sharp. The metallic/dielectric row difference in this unweighted debug signal was not clearly separable from positional differences. This is consistent with the current contract: visible-surface Fresnel and final contribution weighting are owned by `LightPass`, while the deterministic geometry ray uses the mirror direction.

With `Stochastic Rough Sampling` enabled, a 1920x1080 Evaluated Radiance capture showed a strong horizontal change in display-space grain across the roughness conditions. Several spheres and the floor contained dense stochastic samples, while the opposite end of the sphere rows remained substantially more stable and sharp. The capture confirms that the stochastic direction path is active and that its visible variance depends strongly on the roughness condition. The exact screen-left/screen-right mapping to roughness values is not inferred from this capture alone.

The user then identified the screen-space ordering and temporal behavior: the rightmost sphere is roughness `0.0` and remains stable. Temporal noise becomes noticeable at approximately the third sphere from the left, corresponding to roughly roughness `0.35` in the reversed screen-space ordering, and remains apparent toward the rougher end. This is a subjective threshold rather than a measured variance boundary.

The remaining scoped observations were recorded as follows:

- geometry-reflection position or shape changes from frame to frame: PASS;
- the metallic/dielectric difference remains small in Evaluated Radiance: PASS;
- no NaN-like, full-white, or fixed-black failure was apparent: provisional PASS based on the user's "probably yes" observation, not an exhaustive numerical audit.

Convergence and absence of persistent temporal artifacts are not yet established. Fixed ROIs should include the stable roughness `0.0` control and at least one noisy condition at or above the observed roughness threshold.

### 2026-08-23: Diagnostic scene selection contract

- Added `-AutoSelectHybridReflectionEstimatorTest` so the existing linear-HDR diagnostic runner can select the controlled scene explicitly.
- `-ReflectionHdrDiagnostics` still defaults to DamagedHelmet when neither explicit scene-selection flag is supplied, preserving the Phase 1 workflow.
- DamagedHelmet and Estimator Test auto-selection flags are mutually exclusive.
- The HDR diagnostic JSON now records the loaded scene name so reports cannot silently mix scene assumptions.
- Fixed ROI coordinates remain pending a visual overlay check; the automation contract must not embed guessed coordinates.
- Debug x64/HLSL build succeeded with the existing duplicate vcpkg import warning only.

### 2026-08-23: Fixed ROI validation and 64-frame smoke

The initial overlay used a manual capture whose camera framing differed from CLI automation. A first diagnostic run correctly exposed this mismatch because two ROIs returned an all-zero signal. Those coordinates and reports were rejected rather than interpreted as roughness results.

A new 1920x1080 stochastic Evaluated Radiance capture was generated with the same auto-selected scene and hidden-UI path as HDR diagnostics. Three 48x48 upper-metallic-row rectangles were placed inside sphere surfaces and away from silhouettes. The user confirmed all three positions and roughness differences:

| ID | Rectangle | Condition |
| --- | --- | --- |
| `roughness_1_metal` | x `484`, y `396`, width `48`, height `48` | high-variance roughness `1.0` |
| `roughness_035_metal` | x `844`, y `396`, width `48`, height `48` | subjective noise-threshold roughness `0.35` |
| `roughness_0_metal_control` | x `1392`, y `396`, width `48`, height `48` | stable mirror-limit control |

Each ROI then completed an independent 32-frame warm-up plus 64-frame measurement with stochastic sampling enabled and history weight `0.9`.

| ROI | Evaluated mean | Evaluated variance | Evaluated CV | Frame-difference p99 | Evaluated maximum | Resolved variance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness `1.0` | `0.245015` | `1.569857` | `5.113725` | `11.969035` | `12.061967` | `0.0630104` |
| roughness `0.35` | `0.138602` | `0.0365103` | `1.378601` | `0.286579` | `12.077592` | `0.00142675` |
| roughness `0.0` | `0.189160` | approximately `0` | approximately `0` | `0` | `0.664890` | approximately `0` |

The result reproduces the subjective ordering: roughness `0.0` is deterministic and stable, roughness `0.35` has measurable temporal variance, and roughness `1.0` has much larger variance and frame differences. This is a diagnostic smoke result, not an estimator-correctness or convergence claim. All runs reported the expected scene name, 64 frames, error count zero, and three repetitions of the existing committed-buffer initial-state warning type. Hit/accept/depth-reject/normal-reject rates were present in every report.

## Out of Scope

- production temporal/spatial denoiser;
- DLSS RR backend integration;
- Path Tracing pass;
- broad RenderGraph refactor;
- promotion of the default-off Surface Variance Filter.
