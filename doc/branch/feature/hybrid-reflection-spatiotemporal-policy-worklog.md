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
- Added `-ReflectionSpatiotemporalSpatialPolicy` for reproducible automation. It does not implicitly enable the spatial pass, so paired runs can keep `-ReflectionSurfaceVarianceFilter` identical and vary only the policy flag.
- The first 64-frame paired run showed exact policy bypass: confidence remained zero because its update was incorrectly nested under the separate `Variance-Guided Temporal` consumer toggle. Moments still reported variance, confirming that missing evidence production rather than a zero-variance ROI caused the bypass.
- Separated confidence production from temporal-weight consumption. Accepted history now updates confidence metadata in all modes; `Variance-Guided Temporal` still exclusively controls whether that evidence changes the temporal history weight. With that toggle off, temporal RGB remains unchanged.
- Repeated the paired estimator-scene ROI after the separation. The 64-frame run matched sample/temporal indices; compared with the fixed filter, the bounded policy had 3.92% higher temporal variance, 16.96% lower frame-difference p95, 10.03% lower p99, and a 0.041% ROI mean difference.
- The 256-frame standard gate reproduced the direction: sample/temporal indices matched, temporal variance was 6.74% higher, frame-difference mean/p95/p99 were 14.50%/16.61%/10.31% lower, and the ROI mean difference was 0.026%.
- These results support a bounded-tail policy interpretation, not a minimum-variance claim. They cover one controlled 48x48 ROI and do not establish multi-scene generalization or physical correctness.
- Updated the paired report to schema version 2 with explicit variant labels and a signed B-relative-to-A variance-change field. Legacy filter-off/on fields remain for compatibility.
- Extended the 64-frame controlled gate across the known metallic roughness 0.0, 0.35, and 1.0 ROIs. Every pair matched sampling and temporal-frame indices, and every resolved-radiance control was identical.
- At roughness 0.0, the bounded policy bypassed to the resolved control with zero confidence. The fixed filter increased temporal variance by approximately 8.4x in the ROI, while the policy reduced variance by 88.08% relative to that fixed path. The ROI mean difference was 0.013%, and frame-difference p95 was unchanged.
- At roughness 1.0, the policy had 7.62% higher temporal variance than the fixed filter but reduced frame-difference mean/p95/p99 by 19.43%/24.04%/0.47%. The ROI mean difference was 0.018%.
- Together with the roughness 0.35 result, the controlled gate supports the intended behavior: bypass stable mirror evidence, and bound spatial mixing in rough high-confidence regions while preserving the long-run mean. Subjective Lit quality and additional scene coverage remain pending.
- Ran the policy-enabled 64-frame runtime audit on the Estimator Test scene. The process exited with code zero, reported zero D3D12 errors, and repeated only the three known buffer initial-state warnings for this controlled scene.
- Final code audit confirmed that the policy remains default-off, changing its toggle does not reset temporal history, and `ReflectionDenoisedRadiance` never feeds the temporal generation.
- Loaded `Hybrid Reflection Spatial Filter Test` in the live application for a later Lit A/B review. Subjective evaluation is intentionally deferred; implementation, automated measurement, build, and runtime gates are complete.
- Completed the deferred live Lit A/B review on `Hybrid Reflection Spatial Filter Test` with stochastic sampling enabled, history weight `0.9`, the edge-aware spatial pass enabled, and variance-guided temporal disabled. The reviewer could not perceive a difference between the fixed-filter A variant and the bounded-policy B variant.
- Classify the subjective result as PASS WITH LIMITATION: no visible regression, brightness loss, blur, leakage, lag, or instability was identified, but no subjective improvement was established. The measurable claim remains limited to the paired HDR statistics.

Status: done
