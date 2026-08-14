# Hybrid Reflection Material Variance Work Log

Japanese version: [Hybrid Reflection Material Variance Work Log (Japanese)](hybrid-reflection-material-variance-worklog_j.md).

This log records diagnosis of persistent surface-wide stochastic variance in selected DamagedHelmet texture regions. Reflection signal and history semantics remain defined by [Hybrid Reflection Contracts](hybrid-reflection-contracts.md).

## 2026-08-14: Phase Start

- Started `features/hybrid-reflection-material-variance` from `main` at merge commit `a749d80` after closing the edge-stability phase.
- Reproduction regions are the rearward-facing surface adjacent to the smooth crown panel and the exposed underside pipes.
- DamagedHelmet has one glTF material, `Material_MR`, on one primitive. Treat the regions as texture-defined areas, not separate material IDs.
- The symptom covers portions of a surface rather than only edges and remains visible after camera motion stops.
- Keep stochastic sampling disabled and temporal history weight `0.0` as global defaults. The rejected-pixel neighborhood experiment also remains default-off.

## Initial Diagnostic Scope

1. Capture matched views for visible roughness, metallic, normal, evaluated radiance, resolved radiance, and temporal validity.
2. Identify whether both reproduction regions share roughness, metallic, normal-map frequency, hit/miss behavior, or high-radiance environment sampling.
3. Separate persistent input variance from history rejection. A green temporal-validity region can still retain variance when one sample per frame converges too slowly.
4. Prefer observation-only instrumentation or capture metadata before changing sampling or filtering policy.
5. If one cause is supported, test at most one default-off policy in a later measured slice.

## Out of Scope

- broad spatial denoise;
- weakening depth or normal rejection thresholds;
- DLSS Ray Reconstruction or Streamline integration;
- PathTracing;
- global-default changes;
- stacking another edge-stability policy into the previous experiment.

## Decision Gate

- If variance tracks high roughness or textured roughness, investigate sampling variance rather than material identity.
- If variance tracks normal-map frequency, verify direction stability before filtering radiance.
- If history is consistently accepted but noise persists, measure convergence and sample variance before changing rejection.
- If the two regions do not share a cause, split them rather than forcing one combined fix.
