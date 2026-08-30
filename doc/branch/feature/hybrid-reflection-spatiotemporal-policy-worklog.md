# Hybrid Reflection Spatiotemporal Policy Worklog

## 2026-08-30

- Started `features/hybrid-reflection-spatiotemporal-policy` from the merged edge-aware spatial-filter baseline.
- Audited the existing metadata boundary. `ReflectionSpecularConfidence` is persistent high-variance evidence, not history validity or correctness probability. `ReflectionSpecularMoments` contains temporal luminance moments of the weighted estimator.
- Kept temporal-history ownership unchanged. The spatial output remains stateless and never feeds reflection history.
- Added the `Spatial Policy Inputs` debug view without adding a persistent GPU resource. The view recomputes the current spatial neighborhood gates and displays R=confidence, G=mapped temporal variance, and B=accepted non-center neighbor fraction.
- The view is observability only. It does not claim that filtering was applied, that accepted neighbors are unbiased, or that the estimator is physically correct.
- Debug x64 and HLSL compilation succeeded. The only build diagnostic was the pre-existing vcpkg duplicate-import warning.
- Added a separate default-off `Spatiotemporal Spatial Policy` toggle while preserving the fixed-filter comparison path.
- When enabled with the spatial pass, policy strength multiplies persistent confidence, relative standard deviation, and visible roughness gates and caps the blend at `0.75`. Existing depth, normal, roughness, hit/miss, hit-distance, and hit-normal rejection remains authoritative.
- The policy stays stateless and post-temporal. Toggling it does not invalidate or consume spatial output as history.
- Added the policy state to HDR diagnostic report schema version 14.
- Rebuilt Debug x64 after the policy change; C++ and all affected HLSL compiled successfully with only the existing vcpkg duplicate-import warning.
- The default-off DamagedHelmet runtime smoke exited with code zero, captured successfully, and reported zero D3D12 errors. It repeated the two known buffer initial-state warnings and introduced no unknown warning.

Status: in progress
