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

## 2026-08-28: Diagnostic schema v11 baseline

The audit confirmed that the existing resolved-radiance alpha channel already distinguishes no history, outside-history reprojection, depth rejection, normal rejection, and accepted history. The HDR report previously exposed only acceptance, depth rejection, and normal rejection rates. `ReflectionRayHit.r` already carries hit distance, so no new ray payload is required.

The smallest report extension therefore:

- separates `noHistoryRate` and `outsideHistoryRate` from the existing temporal status metadata;
- reports hit-conditioned `meanHitDistance` from the existing ray-hit payload;
- reads the existing `GBuffer.MotionVector` ROI and reports mean and maximum stored NDC-vector magnitude;
- documents that the reported motion values are the stored values before temporal jitter and configured-offset removal;
- increments the HDR diagnostic report schema from 10 to 11 without changing rendering or temporal policy defaults.

Validation:

- Debug x64 and HLSL build: PASS, 0 errors; the existing duplicate vcpkg import warning remains.
- Eight-frame controlled-scene GPU smoke test: PASS; schema 11 and all added fields were emitted.
- Stationary control result: motion magnitude 0, temporal acceptance rate 1.0 after warm-up, and all rejection rates 0, as expected.
- D3D12 Debug Layer: 0 errors. Three pre-existing warnings reported ignored initial UAV state for buffers; no new diagnostic-copy warning was observed.

This checkpoint does not yet measure dynamic motion or settling. Next: add a deterministic camera-motion timeline and calculate settling metrics from the existing per-frame linear-HDR means.

## 2026-08-28: Deterministic camera timeline and settling contract

HDR diagnostics now reuse `-ReflectionOrbitDegrees` and `-ReflectionOrbitFrames` to define a deterministic measurement timeline after warm-up:

1. orbit forward for the configured frame count;
2. reverse for the same frame count and return to the initial yaw;
3. remain stationary for the rest of the measurement.

The automation path explicitly updates the arcball camera after changing its state. This is required for hidden, non-foreground automation runs and also corrects the existing screenshot/keyframe automation path. Each diagnostic frame records its automation-frame index, phase, and yaw offset. Report schema 12 adds the timeline and settling contract.

Settling is measured from the resolved-radiance ROI mean luminance. The settled value is the mean of the final eight stationary samples. T50, T90, and T95 are the first offsets after stop at which the remaining absolute error stays below 50%, 10%, and 5% of the initial stop error for three consecutive samples. The metric is marked invalid when the initial error is no greater than 1% of the settled mean or `1e-6`, because no meaningful response amplitude exists to normalize.

GPU validation used the controlled estimator scene, a 10-degree forward/reverse orbit, history weight 0.9, stochastic sampling, and a 32x32 ROI:

- moving frames reported nonzero mean motion of approximately 0.0024 to 0.0028 NDC in the shorter smoke run;
- reverse frames produced approximately 0.9% to 1.4% depth rejection and no material normal rejection in that ROI;
- stationary frames returned to zero motion and 1.0 history acceptance;
- the 32-sample settling run had only `3.4e-5` initial stop error versus a `7.9e-4` minimum meaningful threshold, so settling was correctly reported as invalid rather than inventing a latency value;
- Debug x64/HLSL build passed with 0 errors; D3D12 Debug Layer showed no new errors or warnings.

Next: run stronger and spatially varied controlled-scene cases, then determine whether a measurable dynamic response exists before applying the Lit perceptual gate.

## 2026-08-29: 30-degree controlled-scene dynamic measurements

The three visually validated 48x48 metallic-row ROIs were measured with a stronger 30-degree orbit, 12 forward frames, 12 reverse frames, history weight 0.9, stochastic sampling, and 32 warm-up frames. Roughness 1.0 and 0.0 used 48 measurement samples. Roughness 0.35 was extended to 64 samples because its preliminary 48-sample window did not provide three consecutive T95 samples. The extended run reproduced the first 48 resolved-mean samples exactly.

| ROI | Mean moving motion (NDC) | Mean moving acceptance | Mean moving depth reject | Mean moving normal reject | Stationary acceptance | T50 | T90 | T95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness 1.0 metal | 0.003949 | 0.9572 | 0.0424 | 0.0004 | 1.0000 | 6 | 11 | 13 |
| roughness 0.35 metal | 0.013917 | 0.9407 | 0.0590 | 0.0003 | 1.0000 | 6 | 19 | 20 |
| roughness 0.0 metal | 0.014832 | 0.9494 | 0.0439 | 0.0067 | 1.0000 | 6 | 15 | 17 |

The motion-magnitude differences are primarily screen-position and projected-motion differences; they must not be attributed to roughness alone. No outside-history rejection occurred in these ROIs. All three cases returned to zero motion and full history acceptance after stop. Their settling response was measurable, with T90 between 11 and 19 frames and T95 between 13 and 20 frames.

A pure fixed EMA at weight 0.9 retains 10% of old history after approximately 22 frames and 5% after approximately 29 frames. These ROI results settle faster because the measured signal includes reprojection/rejection and spatially changing content, not only an uninterrupted scalar EMA. Under this controlled 30-degree camera orbit, the data does not show an excessive long-duration history tail. This is a camera-motion result, not an object-motion conclusion, and it does not by itself establish perceptual acceptability in Lit output.

All four processes exited normally. D3D12 Debug Layer produced zero errors and only the three previously known buffer initial-state warnings per process.

Next: perform the live Lit perceptual gate for the same forward/reverse/stop behavior. Only if Lit exposes a practical artifact should object-motion or hit-identity diagnostics be promoted ahead of the planned follow-up.
