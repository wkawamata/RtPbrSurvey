# Hybrid Reflection Production Quality Gates Worklog

## 2026-08-30

- Started `features/hybrid-reflection-production-quality-gates` from merge commit `ac5f6f0` on current `main`.
- Audited current defaults, controlled scenes, prior quantitative reports, runtime audits, and scoped subjective results.
- Defined separate production-baseline, production-candidate, diagnostic-experiment, and rejected-experiment classes.
- Fixed four evaluation profiles so variance-guided temporal is not silently combined with the spatial-policy comparison.
- Recorded the minimum scene coverage and acceptance gates. Current stochastic, temporal, fixed-spatial, bounded-spatial, and variance-guided features remain diagnostic/default-off; this phase has not promoted them.
- Added five versioned named ROI profiles covering controlled metallic roughness `0.0`, `0.35`, and `1.0` plus the two established DamagedHelmet noise regions. This removes manual coordinate copying from production-gate runs.
- Added `Invoke-ProductionQualityGates.ps1`. It validates paired sample/temporal sequences, unchanged resolved-radiance control variance, and a predeclared `0.5%` mean-preservation bound while leaving variance/tail changes as observations.
- An ad hoc roughness probe used an incorrect coordinate and produced a materially different result; it is explicitly excluded from gate evidence. The named `estimator-metal-r035` 64-frame smoke reproduced the established result: matching sequences, `+3.92%` temporal variance, `-16.97%` frame-difference p95, `-10.03%` p99, and `0.041%` mean difference. All invariants passed.

Status: in progress

