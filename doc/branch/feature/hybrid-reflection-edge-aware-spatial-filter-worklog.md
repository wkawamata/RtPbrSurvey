# Hybrid Reflection Edge-Aware Spatial Filter Work Log

## 2026-08-29: Implementation boundary audit

- Started `features/hybrid-reflection-edge-aware-spatial-filter` from `main` at `91220dd`.
- Confirmed that the existing default-off `Surface Variance Filter` runs inside `TemporalReflectionPass`, filters `ReflectionEvaluatedRadiance` before temporal accumulation, and gates neighbors using visible depth, normal, roughness, and metallic only.
- The existing experiment is not the new denoiser-contract spatial boundary: it has no distinct output, does not consume resolved radiance, and does not use reflection hit class or hit distance.
- The first implementation slice will add a distinct `ReflectionDenoisedRadiance` resource and a stateless `EdgeAwareSpatialReflectionPass` after temporal resolution. Disabled operation must preserve the current pass graph and LightPass input.
- Spatial output will not feed temporal history. This preserves filter-off/on observability and the temporal-history ownership fixed by PR #40.

## 2026-08-29: Independent spatial pass implementation

- Added transient `ReflectionDenoisedRadiance`, a stable SRV/RTV binding, and a separate `EdgeAwareSpatialReflectionPass` after `TemporalReflectionPass`.
- Moved the existing default-off setting from the old pre-temporal filter branch to the new post-temporal pass. Disabled frames omit the pass and bind `ReflectionResolvedRadiance` directly to `LightPass`.
- The stateless 3x3 kernel requires compatible visible depth, visible world normal, visible roughness, hit/miss class, hit distance, and decoded hit normal. Near-mirror pixels bypass filtering.
- Spatial output alpha is `1`; temporal-validity metadata stays on `ReflectionResolvedRadiance.a`. Spatial output never feeds temporal history, and toggling the pass no longer invalidates history.
- Debug x64 and affected HLSL succeeded with zero errors and one known duplicate-vcpkg-import warning.
- Matched controlled-scene Lit smoke captures completed for filter off/on with exit code 0. Both runs had zero D3D12 errors and three known committed-buffer-state warnings. The on capture showed a bounded reduction in rough-sphere grain without an obvious silhouette leak; quantitative and subjective quality gates remain pending.

## 2026-08-29: Linear-HDR paired development gate

- Extended the HDR diagnostic capture with a separately reported `ReflectionDenoisedRadiance` signal. When the filter is disabled, this field reads `ReflectionResolvedRadiance` as an explicit identity fallback; the temporal signal remains separately reported as the control.
- Updated the paired runner to select the test scene explicitly and compare the spatial output rather than incorrectly treating the unchanged temporal output as the filter result.
- The 64-frame roughness `0.35` metallic controlled ROI used identical sample and temporal index sequences. The spatial output reduced temporal variance by `29.2514%` while changing mean luminance by `0.1488%`.
- The off/on `ReflectionResolvedRadiance` control variance was identical at report precision. This confirms that the new spatial pass does not feed or perturb temporal history in the measured static case.
- This is a development-level, current-estimator-relative gate. It does not establish physical correctness, production denoiser readiness, motion quality, or cross-scene generalization. A 256-frame PR gate and live subjective evaluation remain future work.

## 2026-08-30: DamagedHelmet grazing-angle emissive-reflection observation

- A narrow yellow emissive reflection was identified on the helmet side adjacent to an emissive material when viewed at a grazing angle. The directly visible emissive source and its reflected image were distinguished by their surfaces and viewing geometry.
- With Stochastic Rough Sampling enabled and Temporal History Weight `0.0`, temporal grain was clearly visible in the Lit reflection.
- Raising Temporal History Weight to `0.9` removed the subjectively visible temporal noise while preserving the emissive reflection.
- This establishes a reproducible Lit observation point on DamagedHelmet. Weight `0.0` is suitable for isolating the spatial filter, while weight `0.9` is suitable for checking whether the spatial pass adds useful quality or only removes detail after temporal stabilization.
- Motion response, filter-off/on detail preservation, and cross-scene generalization are not yet claimed.

## 2026-08-30: DamagedHelmet Lit A/B/C/D subjective gate

- The camera was held at the identified grazing-angle view of the narrow yellow emissive reflection. Stochastic Rough Sampling and Hybrid Reflection contribution remained enabled throughout.
- A (`weight 0.0`, spatial off) established clearly visible temporal grain.
- B (`weight 0.0`, spatial on) looked identical to A. No noise reduction, darkening, blur, or yellow leakage was subjectively distinguishable.
- C (`weight 0.9`, spatial off) visibly reduced the grain relative to A, preserved the emissive reflection, and showed no objectionable lag or ghosting.
- D (`weight 0.9`, spatial on) looked identical to C. No additional improvement or regression was subjectively distinguishable.
- This scoped Lit gate supports the temporal history policy but does not demonstrate perceptual value for the spatial filter on DamagedHelmet. The filter remains default-off. Its controlled linear-HDR variance reduction is retained as diagnostic evidence only.

## 2026-08-30: Dedicated spatial-filter diagnostic scene

- Added `Hybrid Reflection Spatial Filter Test` as a procedural scene dedicated to separating surface-interior smoothing from boundary preservation.
- Four cube-and-sphere pairs are partially embedded into a dark floor. Cube/floor intersections provide straight edges at several projected angles; spheres partially embedded into cubes provide curved material and geometry boundaries.
- The pairs cover same-roughness metallic/dielectric and albedo changes, a roughness discontinuity, and a near-mirror receiver beside a rough metallic receiver. The same-roughness cases deliberately expose that the current filter does not gate visible metallic, albedo, or material identity.
- Alternating cube rotations create horizontal, vertical, and diagonal projected edges. Large yellow and cyan emissive targets provide high-contrast reflected signals and make cross-boundary leakage easier to identify.
- The intended isolated comparison is Stochastic Rough Sampling enabled, Temporal History Weight `0.0`, and spatial filter off/on. Weight `0.9` remains a second-stage temporal-plus-spatial interaction check.
- Scene framing, material readability, stochastic-noise visibility, and filter-off/on behavior still require runtime validation.

## 2026-08-30: Spatial-effect interpretation from the diagnostic scene

- The visible filter effect was identified primarily inside a single material surface: temporally changing bright emissive-reflection samples were spatially softened, reducing their granular appearance.
- Material and geometry boundaries are therefore not the source of the desired effect. They are safety gates used to verify that the spatial average does not leak radiance across unrelated surfaces.
- The pass remains spatial and stateless; it does not remove temporal noise by accumulating time. It reduces the per-frame contrast and particle-like appearance of samples whose positions or intensities vary over time.
- Future evaluation should distinguish the primary same-surface grain-reduction observation from secondary straight/curved edge-preservation checks.

## 2026-08-30: Dedicated-scene subjective result

- With Stochastic Rough Sampling enabled, Temporal History Weight `0.0`, and the spatial filter disabled, granular temporal noise was clearly visible.
- Enabling the filter slightly softened individual particle edges but did not reduce the overall granular impression. A perceptually meaningful denoise improvement is therefore not claimed.
- Average emissive-reflection brightness remained acceptable.
- No color leakage was observed across cube/floor straight edges, sphere/cube curved edges, or metallic/dielectric boundaries.
- The result supports the implemented edge-rejection safety behavior under the evaluated conditions, while showing that the fixed 3x3 kernel is too limited to materially suppress the observed particle field. The pass remains default-off and diagnostic-only.
- The scene also made Stochastic Reflection behavior substantially easier to inspect than DamagedHelmet because multiple large emissive targets produced visible high-contrast reflected samples. It is a stress/diagnostic scene, not a representative production scene.

Status: done
