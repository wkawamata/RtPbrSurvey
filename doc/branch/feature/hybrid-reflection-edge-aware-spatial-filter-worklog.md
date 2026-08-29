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

Status: done
