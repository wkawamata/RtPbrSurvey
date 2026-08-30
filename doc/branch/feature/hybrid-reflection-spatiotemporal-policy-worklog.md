# Hybrid Reflection Spatiotemporal Policy Worklog

## 2026-08-30

- Started `features/hybrid-reflection-spatiotemporal-policy` from the merged edge-aware spatial-filter baseline.
- Audited the existing metadata boundary. `ReflectionSpecularConfidence` is persistent high-variance evidence, not history validity or correctness probability. `ReflectionSpecularMoments` contains temporal luminance moments of the weighted estimator.
- Kept temporal-history ownership unchanged. The spatial output remains stateless and never feeds reflection history.
- Added the `Spatial Policy Inputs` debug view without adding a persistent GPU resource. The view recomputes the current spatial neighborhood gates and displays R=confidence, G=mapped temporal variance, and B=accepted non-center neighbor fraction.
- The view is observability only. It does not claim that filtering was applied, that accepted neighbors are unbiased, or that the estimator is physically correct.
- Debug x64 and HLSL compilation succeeded. The only build diagnostic was the pre-existing vcpkg duplicate-import warning.

Status: in progress
