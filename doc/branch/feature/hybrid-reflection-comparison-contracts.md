# Hybrid Reflection Comparison Contracts

## Purpose

This document fixes the signal, scene-state, presentation, metric, and claim contracts for comparisons among Raster, Hybrid Reflection, future Path Tracing (PT), and future DLSS Ray Reconstruction (RR). It prevents unimplemented PT/RR backends from being described as current functionality and prevents buffers with different meanings from being compared as equivalent images.

## Comparison Boundaries

| Boundary | Current signal | Semantics | Allowed comparison |
|---|---|---|---|
| Current reflection | `ReflectionEvaluatedRadiance.rgb` | Linear HDR, current frame, before temporal processing and before LightPass distance/visible-roughness/intensity/Fresnel weighting | Sampling, hit/miss, current estimator, and a future equivalent one-bounce PT signal |
| Resolved reflection | `ReflectionResolvedRadiance.rgb` | Linear HDR after temporal processing and before final weighting; unweighted semantics are preserved | Temporal/denoise policies. A comparison with current reflection must identify the processing difference |
| Spatial reflection | `ReflectionDenoisedRadiance.rgb` | After the default-off spatial process and before final weighting; never temporal history | Spatial policies and paired filter-off comparisons |
| Final Lit | `LightPass.RenderTarget` | Linear HDR scene color including the reflection contribution and before tone mapping | Practical Raster/Hybrid comparison with all non-reflection lighting held constant |
| Presentation | Output after tone mapping/upscaling | Display-referred output | Final perceptual quality only; not radiance, bias, or energy comparison |

`ReflectionSpecularEstimate` and `ReflectionResolvedSpecularEstimate` are weighted-estimator diagnostics, not unweighted radiance. `ReflectionRayColor` is hit albedo, not reflection radiance. They must not be substituted for the radiance boundaries above.

## Mode Matrix

| Mode | Current status | Reflection-boundary comparison | Final-Lit comparison |
|---|---|---|---|
| Raster baseline | Implemented | No direct comparison because Raster has no equivalent ray-radiance signal | Hybrid-off `LightPass.RenderTarget` is the baseline |
| Hybrid deterministic | Implemented | `ReflectionEvaluatedRadiance`; stochastic off, history weight `0.0` | Hybrid on, experimental filters off |
| Hybrid stochastic current | Default-off diagnostic | Explicitly select `ReflectionEvaluatedRadiance` or `ReflectionSpecularEstimate` by name | Stochastic on, history weight `0.0` |
| Hybrid resolved | Default-off diagnostic | Explicitly select `ReflectionResolvedRadiance` or `ReflectionDenoisedRadiance` by name | Record the complete temporal/spatial configuration |
| Path Tracing | Not implemented | Future comparison only when bounce scope and unweighted/weighted semantics match | PT Lit output with the same scene, camera, and exposure |
| DLSS RR | Not implemented | Not comparable until a separate contract fixes the backend-required/produced signals | Paired RR on/off within the same render path; do not mix with an SR difference |

A high-sample PT image is not automatically ground truth. It becomes a physical-reference candidate only when the declared integrator, bounce count, light transport, material model, environment, clamping, sample count, and signal meaning match the target comparison. A high-sample mean of the current estimator is the `Current-Estimator Mean Baseline`, not physical GT.

## Fixed Scene State

Paired comparisons keep the following identical:

- scene asset, node visibility, world transforms, and animation time;
- camera mode, view/projection, FOV, near/far, and Arcball pivot/distance;
- render resolution, output resolution, viewport, crop, and ROI;
- lights, IBL, emissive inputs, materials, and reflection distance/intensity settings;
- exposure, tone-map mode, and HDR/SDR output mode;
- frame timeline, warm-up, measurement range, and history-reset point;
- stochastic sample index/sequence; if identical random numbers have no shared meaning across estimators, record that limitation;
- temporal, spatial, upscaler, and RR enable states and presets.

Camera/object-motion comparisons use the same continuous timeline. A still image cannot pass temporal stability, ghosting, or settling gates.

## Exposure and Presentation

Linear-HDR metrics are computed from the corresponding signal before tone mapping, display transfer, UI overlays, and presentation scaling. Final-Lit HDR comparisons use the same fixed exposure. Auto exposure requires a separate profile that records the exposure value or exposure-texture timeline.

SDR/HDR10 screenshots support subjective review; they do not establish linear-radiance mean, variance, bias, or energy preservation. Images with DLSS SR or another upscaler are separated from native-resolution comparisons and declare both render and output resolution.

## Metric Contract

| Metric | Domain | Use / limitation |
|---|---|---|
| Mean luminance and mean relative difference | Linear HDR, corresponding ROI | Mean preservation; do not use a relative value alone for near-zero ROIs |
| Variance, standard deviation, coefficient of variation | Linear HDR over time | Static noise; fix sequence and frame range |
| Frame absolute-difference mean/p95/p99 | Linear HDR over time | Flicker/tails; separate motion and static intervals |
| RMSE | Same-semantic linear-HDR boundary | Always name the reference and sample conditions |
| T50/T90/T95 | Linear-HDR ROI response | Settling after motion; valid only with sufficient signal amplitude |
| Hit/accept/reject rates | Diagnostic metadata | Cause explanation, not quality or correctness probability |
| GPU time and memory | Declared hardware/resolution | Production cost; unmeasured performance is `NOT CLAIMED` |
| Subjective A/B | Identical presentation conditions | Perceptual quality; no visible difference establishes only non-regression |

## Capture and Report Metadata

A comparison artifact set records at least source revision, scene, mode, signal boundary, resolutions, camera, exposure, frame range, warm-up, sample sequence, history reset, temporal/spatial/upscaler settings, ROI, and metric domain. The application report need not own source-control discovery when the execution harness emits a revision-bearing validation manifest. An A/B label must never be the only source of configuration meaning.

## Claim Boundary

Current code can support claims about the Raster Final-Lit baseline, implemented Hybrid signals, and diagnostic A/B comparisons under matched conditions. PT and DLSS RR quality, correctness, speedup, and comparability remain `NOT CLAIMED`. DLSS SR output must not be described as RR output.

This branch fixes comparison contracts. It does not add a PathTracing pass, RR backend, Streamline SDK types, a large RenderGraph refactor, or production-default changes.
