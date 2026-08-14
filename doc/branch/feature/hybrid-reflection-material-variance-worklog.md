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

## 2026-08-14: Matched Diagnostic Capture

- Added `-ReflectionCaptureDebugView <name>` as a capture-only view selector. It preserves the existing Hybrid Reflection automation and supports `pbr-params`, `normal`, `hit-material`, `evaluated-radiance`, `resolved-radiance`, and `temporal-validity`.
- Added `capture-plan-material-variance.json` with one settling capture at frame 195. All six runs used the same camera timeline, camera distance scale `0.5`, stochastic sampling enabled, and temporal history weight `0.9`.
- The PBR capture encodes visible metallic, roughness, and ambient occlusion in RGB. The normal capture shows the visible GBuffer normal. The hit-material capture is the ray-hit payload, so it must not be interpreted as the visible-surface material.
- Evaluated radiance contains obvious surface-wide sample variance. Resolved radiance is visibly smoother, confirming that temporal accumulation is active and materially reduces the variance.
- Temporal validity is accepted across the settled frame. This does not prove every earlier frame was accepted, but it argues against persistent history rejection as the sole cause of the remaining static noise.
- The rearward region and underside pipes both contain textured PBR and normal variation. The captures do not yet justify attributing both regions to one material channel or one shared policy.

### Current Decision

- Treat the remaining symptom as slow convergence of accepted stochastic input unless a temporal sequence shows otherwise.
- Do not weaken rejection thresholds and do not enable the rejected-pixel neighborhood experiment globally.
- The next measured slice should quantify frame-to-frame variance in fixed regions of interest, separately for evaluated and resolved radiance.

### Validation

- Debug x64 MSBuild succeeded with 0 errors and the existing duplicate vcpkg import warning.
- Six automated captures completed successfully.
