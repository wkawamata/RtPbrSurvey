# Part 5: PT input buffer validation

## Minimal reproduction and instrumentation proposal

Base: 9455eec; branch: codex/path-tracing-validation-part5.
Existing -DebugPreviewResource changes the inspector display, but PNG captures the composed display and PFM always captures normalized accumulation RGB. Neither preserves the selected guide channels. Proposed scoped instrumentation: a .ptbuf capture selected by the existing -DebugPreviewResource option; use the existing readback and fence, preserve native half/float values and all channels, and save the exact current/previous camera matrices in the header. No estimator or shader changes.

CameraState already supports lensShiftX/Y, but scene JSON rejects them and cannot reproduce PR83 off-center projection. Expose optional finite lensShiftX/Y in scene documents, default zero, and forward to the existing camera implementation. No projection algorithm or camera API change.

Potential ViewZ issue to measure: ComputePrimaryViewZ obtains forward from the inverse-projected NDC center at far Z. With lens shift, this is an off-axis ray instead of the camera view axis. A front plane at view distance 5 can therefore have spatially varying ViewZ. Compare saved values against both camera-axis depth and that shader direction; report the discrepancy without fixing the shader.

## Capture automation reproduction

The initial 20-run cohort in bin/PathTracingValidation/part5-inputs preserves four requested moving captures that did not move: -SceneFile input-plane/scene.json -ReflectionOrbitDegrees 8 -ReflectionOrbitFrames 8 -CaptureAfterFrames 30 kept camera [0,0,-5], target [0,0,0] and identical current/previous matrices. Do not count these as camera-motion evidence. OpenFileScene bypasses the Arcball initialization and CLI debug inspector opening performed for sample scenes. Minimal proposal: reuse the existing Arcball initialization in OpenFileScene and share the existing inspector-opening code, without changing camera math or shaders. Repeat the cohort in a fresh directory and require nonzero expected projected motion for requested moving captures.

## Pre-final precision policy

The retained orbit smoke (part5-orbit-smoke) moved the shifted camera to [-0.6958655,0,-4.9513402]. Motion agreed in sign and magnitude, but its max absolute error 3.0792e-5 NDC exceeded the initial 2e-5 threshold. The initial threshold result remains in every report. For the final cohort, also test each component against 2 ULP of its expected FLOAT16 value + 3e-6 NDC for float ray/geometry/matrix arithmetic. Half spacing scales with magnitude (3.05176e-5 around 0.032), so a fixed 2e-5 bound cannot cover that format across larger motion. This is an explicit conservative storage/computation bound, not an estimator change. Record max normalized error; any violation remains a failure. Static expected zero uses the same rule (~3.12e-6).

Albedo's standard-sRGB expectation and 0.0015 threshold remain unchanged. Separately compare the implemented pow(UNORM,2.2) definition against the same threshold. Preserve the standard-sRGB discrepancy instead of passing it by increasing tolerance.
