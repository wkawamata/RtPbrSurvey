# Hybrid Reflection Multi-Scene Quality Gates Worklog

## 2026-08-29: Branch baseline and gate contract

Branch: `features/hybrid-reflection-multi-scene-quality-gates`

Base: `2147cc6` (`Add Hybrid Reflection dynamic temporal diagnostics (#36)`)

This branch turns the existing individually validated ROIs into one repeatable non-subjective multi-scene gate. It does not replace the deferred Lit perceptual evaluation and does not modify renderer quality policy.

The versioned manifest covers four dynamic cases:

- controlled Estimator Test metallic spheres at roughness 1.0, 0.35, and 0.0;
- the DamagedHelmet underside-pipe region.

All cases use linear-HDR schema 12 diagnostics, stochastic sampling, history weight 0.9, a deterministic forward/reverse/stop camera timeline, exhaustive temporal-status reporting, and D3D12 Debug Layer logging. Scene-specific orbit magnitude and measurement-window length remain explicit manifest data.

The aggregate gate requires:

- successful process completion and a complete report;
- schema/timeline/rate/settling contract validation;
- no D3D12 errors or unknown warnings;
- known warning count no greater than the recorded baseline;
- stationary history acceptance at least 0.99;
- moving outside-history rate no greater than 0.10;
- valid T95 no greater than 30 frames for controlled cases;
- valid T95 no greater than 40 frames for the DamagedHelmet underside-pipe case.

DamagedHelmet settling validity is optional because a fixed screen-space textured ROI can have insufficient stop-response amplitude. This gate does not claim perceptual quality, physical ground truth, object-motion correctness, scene-wide coverage, or production denoiser readiness.

## 2026-08-29: Execution result and final scope

The first suite run showed that a locally saved interactive camera override could make the DamagedHelmet ROIs point at the background. HDR diagnostics now apply the versioned scene default before initializing their ROI and camera timeline, making automation independent of user settings.

Results with the versioned camera:

| Case | Result | Moving acceptance | Stationary acceptance | T50 / T90 / T95 |
|---|---|---:|---:|---:|
| Estimator r1 metallic | PASS | 0.9572 | 1.0000 | 6 / 11 / 13 |
| Estimator r0.35 metallic | PASS | 0.9407 | 1.0000 | 6 / 19 / 20 |
| Estimator r0 metallic | PASS | 0.9494 | 1.0000 | 6 / 15 / 17 |
| DamagedHelmet underside pipes | PASS | 0.9502 | 1.0000 | 7 / 22 / 34 |

Every case exited with code 0 and reported zero D3D12 errors and zero unknown warnings. Known buffer initial-state warnings numbered three for controlled cases and two for DamagedHelmet.

The old rearward-surface ROI did not acquire a valid signal with the versioned default camera. It was not marked as passing; it was removed from the automated manifest. Camera/ROI redefinition and Lit perceptual confirmation are deferred to the later evaluation branch.

The Debug x64 build passed with zero errors and one known MSBuild import warning; HLSL custom builds were up to date. Subjective evaluation was intentionally not performed. This branch completes only the non-subjective multi-scene regression gate.
