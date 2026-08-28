# Hybrid Reflection Dynamic Temporal Diagnostics Worklog

## 2026-08-28: Branch baseline

Branch: `features/hybrid-reflection-dynamic-temporal-diagnostics`

Base: `a3f052c` (`Add RenderGraph diagnostic node viewer (#35)`)

This branch diagnoses dynamic temporal behavior before any further filtering policy is designed. It does not assume that the previously reported subjective response delay is a confirmed defect. The controlled `Hybrid Reflection Estimator Test` scene is the primary diagnostic scene; DamagedHelmet remains a secondary generalization check.

### Integrated baseline

- Hybrid reflection resource and pass contracts are on `main`.
- Stochastic rough sampling remains experimental and default-off.
- Temporal reflection history uses motion reprojection with depth and normal rejection.
- Persistent confidence and variance-guided temporal weighting remain experimental and default-off.
- The prior live Lit gate found no observed motion, direction-reversal, or settling regression. Static noise brightness may have decreased slightly, but the visible A/B difference was small.
- RenderGraph diagnostics are available for inspecting temporal pass/resource ownership. Reflection history ping-pong nodes now remain visually stable across alternating frames.

### Diagnostic questions

1. Does the motion vector select the expected previous-frame history pixel during camera and object motion?
2. Which condition rejects history: bounds, motion/reprojection, depth, normal, or explicit reset?
3. Does accepted history remain valid when reflection hit distance or hit identity changes?
4. How quickly does resolved radiance settle after motion stops?
5. Are any visible Lit artifacts significant enough to justify changing the temporal contract or policy?

### Planned measurements

- Record per-frame current and previous pixel coordinates for a small fixed ROI/probe set.
- Record history acceptance and individual rejection reasons rather than a single validity bit.
- Record current/previous depth, normal agreement, motion vector, roughness, hit flag, and hit distance.
- Record evaluated and resolved linear-HDR luminance and compute T50/T90/T95 settling frames after a deterministic motion stop.
- Separate camera motion, object motion, direction reversal, disocclusion, and stationary control cases.
- Use RenderGraph diagnostics to confirm ping-pong resource ownership without merging `.0` and `.1` physical resources.

### Gates and claim limits

- PASS: a rejection or acceptance sequence can be explained from recorded inputs.
- PASS: settling metrics are reproducible under a fixed timeline and reset point.
- PASS WITH LIMITATION: a debug-view response difference is recorded separately from Lit perceptual significance.
- NOT CLAIMED: production denoiser readiness, a confirmed object-motion bug, physical ground truth, or a required change to the default history weight.

Next: audit the existing motion-vector, temporal-validation, and HDR diagnostic paths to identify the smallest report-schema extension.
