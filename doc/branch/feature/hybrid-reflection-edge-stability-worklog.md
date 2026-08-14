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

## 2026-08-13: Rejected-Pixel Neighborhood Experiment

- Added a default-off experiment that applies a 3x3 current-frame radiance average only after depth or normal history rejection.
- Neighbor eligibility uses the existing visible-depth tolerance `0.002` and visible-normal dot threshold `0.9`. Accepted-history pixels, history thresholds, and disocclusion decisions are unchanged.
- Changing the experiment setting invalidates reflection history. Synthetic noise remains injected independently per neighborhood sample before averaging.
- Captured enlarged DamagedHelmet A/B checkpoints with stochastic sampling enabled and history weight `0.9`: A disables the experiment and B enables it.
- Preliminary still inspection shows less moving-frame grain in B and nearly identical A/B settling frames. B also slightly smooths fine moving-frame highlights, so adoption requires subjective review rather than an automatic pass.
- Added `suite-edge-stability.json` with three cases and nine criteria covering stability, detail retention, boundary leakage, disocclusion, settling, and brightness. English and Japanese rendering were verified in the local evaluator.
- Debug x64 build succeeded with zero errors. Both automated runs reported no D3D12 errors and the same two pre-existing committed-buffer initial-state warnings.

## 2026-08-14: Mobile Subjective Report

- Published the edge-stability evaluator and its six scoped A/B captures through GitHub Pages for mobile review. The report remains a downloaded user artifact and is not committed.
- Received `hybrid-reflection-edge-stability-v1` report version 1, evaluated on 2026-08-14.
- Mid-motion passed stability improvement, detail retention, and boundary preservation: 3/3.
- Direction reversal passed stability improvement, detail retention, and disocclusion safety: 3/3.
- Settling appearance, persistent detail, and brightness were all marked unable to judge: 0 pass, 0 fail, 3 unable.
- No defect flags or notes were recorded.
- Decision: retain the experiment as default-off. The report validates the intended motion-time benefit with no reported moving-boundary regression, but does not close the settling-side-effect gate.
- Next action is a smaller settling-only review or live stop observation. Do not enlarge the policy or change global defaults before that gate is resolved.

## 2026-08-14: Focused Settling Gate

- Added a reduced settling-only capture plan at 1, 6, and 15 frames after camera motion stops at frame 180.
- Added a three-case, six-criterion bilingual suite limited to detail retention, reflection/brightness preservation, convergence, and persistent artifacts.
- Captured A with rejected-pixel neighborhood disabled and B enabled under the same stochastic, history `0.9`, and enlarged-camera conditions.
- Preliminary inspection shows visible noise reduction with slight fine-detail smoothing at frame 1, a much smaller difference by frame 6, and near-equivalent output by frame 15.
- The new images and suite will be published through the existing GitHub Pages evaluator for the unresolved mobile settling gate.
- No code changed in this slice, so the prior successful Debug x64 build remains applicable and was not repeated.

## 2026-08-14: Settling Gate Result

- Received `hybrid-reflection-edge-settling-v1` report version 1.
- At 1 frame after stop, both criteria were unable to judge. The note states that both A and B retain noise from the top through the rear material.
- At 6 frames after stop, detail and reflection-content criteria both failed.
- At 15 frames after stop, A/B equivalence and persistent-artifact criteria both failed, with the note referring to the same observation as frame 6.
- Aggregate result: 0 pass, 4 fail, 2 unable. No structured defect checkboxes were selected, but the notes identify persistent top/rear material noise affecting both variants.
- User clarification: the observed noise is not primarily an edge artifact. It covers the full surface of particular materials. Classify it as material/surface-wide stochastic variance, separate from the moving-edge rejection problem targeted by this branch.
- Decision: do not adopt the rejected-pixel neighborhood experiment as a production or global-default policy. Its earlier 6/6 motion-time passes are retained as evidence of a localized benefit, but the settling gate failed and the common persistent noise remains unresolved.
- Keep the implementation default-off for reviewability in this branch. Do not respond by weakening rejection thresholds or broadening the filter in this phase.
- This completes the planned first-policy comparison. Further work should be split into a separately scoped investigation of material-wide stochastic variance on the top/rear surfaces rather than stacking another speculative edge policy here.
