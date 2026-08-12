# Hybrid Reflection Edge Stability Work Log

Japanese version: [Hybrid Reflection Edge Stability Work Log (Japanese)](hybrid-reflection-edge-stability-worklog_j.md).

This log records diagnosis, controlled validation, and narrowly scoped stabilization work for residual moving-edge flicker in stochastic Hybrid Reflection. Normative signal and history semantics remain in [Hybrid Reflection Contracts](hybrid-reflection-contracts.md).

## 2026-08-12: Phase Start

- Started `features/hybrid-reflection-edge-stability` from `main` at `6f552d8`.
- The previous phase passed its six-image real-signal suite and live DamagedHelmet A/B gate at history weight `0.9`, while the user observed minor flicker at moving edges.
- Keep stochastic sampling disabled and temporal history weight `0.0` as global defaults during this phase.
- Diagnose before selecting a fix. Do not treat all edge instability as a denoise problem.

## Diagnostic Order

1. Reproduce the failure across controlled rough-metal, glossy-dielectric, compact-bright-reflection, and silhouette/disocclusion cases.
2. Compare history weights `0.5`, `0.75`, and `0.9` under identical camera timelines.
3. Separate these candidate causes:
   - stochastic hit/miss transitions;
   - depth or normal history rejection toggling;
   - nearest-sampled resolved radiance;
   - visible-roughness discontinuities;
   - ordinary disocclusion where history must be rejected.
4. Add observation-only instrumentation before changing rejection or filtering policy when the existing views cannot distinguish the causes.
5. Select at most one minimal stabilization policy for the first implementation slice, then repeat sampled-frame and live A/B validation.

## Initial Scope

In scope:

- repeatable multi-condition validation scenes or camera plans using existing scene infrastructure;
- lightweight debug classification for accepted/rejected history or reflection hit/miss when needed;
- one measured, narrowly scoped adjustment such as radiance-only bilinear history sampling, roughness-aware history weight, threshold policy, reflection-specific rejection, or neighborhood clamp;
- English and Japanese work-log updates in the same commits.

Out of scope:

- a general spatial denoise pass;
- DLSS Ray Reconstruction or Streamline integration;
- PathTracing;
- broad RenderGraph or descriptor architecture changes;
- changing the global stochastic or temporal defaults without broader evidence.

## Decision Gate

- If instability is valid disocclusion rejection, do not weaken rejection merely to make the edge look temporally smooth.
- If history correspondence is valid but nearest radiance sampling causes visible stepping, test radiance-only bilinear sampling while keeping depth/normal validity tests discrete.
- If stochastic hit/miss transitions dominate, evaluate reflection-specific evidence before spatial filtering.
- If no single cause dominates across cases, stop after diagnosis and split follow-up work rather than combining several speculative fixes.
- At the end of the first measured phase, reassess plan size before adding another policy.

## 2026-08-12: Diagnostic Framing

- Added the capture-only `-ReflectionCameraDistanceScale` option. The scale is applied once to the initial Arcball distance and remains fixed across the capture-plan timeline.
- Use scale `0.5` for the DamagedHelmet edge-stability captures. This produces approximately twice the prior linear image size without changing interactive or non-capture camera defaults.
- Captured the three stochastic-plan checkpoints with variant `edgezoom`; the helmet remained inside the frame at mid-motion, reversal, and settling.
- Debug x64 build succeeded with zero errors. The automated run reported no D3D12 errors and retained two pre-existing committed-buffer initial-state warnings.
- Generated PNG files and the runtime log remain local validation artifacts and are not committed.

## 2026-08-12: Temporal Validity Classification

- Added a `Temporal Validity` debug view without allocating another render target. `ReflectionResolvedRadiance.a` carries diagnostic metadata while RGB retains the unweighted resolved-radiance contract and unchanged blend behavior.
- Classification colors are black for no history, blue for reprojection outside history, red for depth rejection, yellow for normal rejection, and green for accepted history.
- Added `-CaptureReflectionTemporalValidity` so the existing capture-plan automation can reproduce the classification view.
- At the enlarged DamagedHelmet mid-motion and reversal checkpoints, normal rejection concentrated on thin geometric and internal-feature edges. Depth rejection appeared only on limited silhouette/disocclusion pixels.
- The settling checkpoint returned to history acceptance across the image. This argues against persistent invalid history and points to valid motion-time rejection exposing the noisy current sample as the leading cause for the observed edge flicker.
- Do not loosen depth or normal thresholds from this evidence alone. The next comparison should preserve valid rejection and test whether rejected pixels can be stabilized without retaining disoccluded history.
- Debug x64 build succeeded with zero errors. The automated run reported no D3D12 errors and the same two pre-existing committed-buffer initial-state warnings.
