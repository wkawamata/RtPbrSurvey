# features/hybrid-reflection-hdr-variance-diagnostics

## Objective

Measure Hybrid Reflection variance and convergence in the linear-HDR signal domain before changing the estimator or promoting any filter. The phase compares the existing default-off Surface Variance Filter against the same current approximate estimator under paired conditions.

This phase may claim only that a policy reduces variance relative to the current estimator while preserving its long-run mean within a measured tolerance. It does not establish physical correctness, unbiasedness, energy conservation, production denoiser readiness, or convergence to a physical reference.

## Measurement Levels

| Frames | Role |
| ---: | --- |
| 64 | Fast development gate |
| 256 | Standard PR evaluation |
| 1024 | Optional extended audit for mean drift, rare fireflies, and bias trends |

The 1024-frame level is not required for every change. Run it only after the 256-frame result or an observed outlier justifies the additional cost.

## Required Metrics

For each fixed ROI and signal boundary, record:

- temporal mean of linear-HDR luminance;
- temporal variance and standard deviation;
- coefficient of variation `sigma / mean`, with an explicit near-zero-mean policy;
- mean, p95, and p99 of absolute frame-to-frame luminance difference;
- RMSE against the High-SPP Current-Estimator Mean Baseline;
- long-run filter-on versus filter-off mean difference;
- ray hit rate;
- temporal acceptance rate;
- depth-rejection rate;
- normal-rejection rate;
- maximum luminance as an auxiliary firefly indicator, not a standalone quality metric.

## Paired Comparison Contract

- Reset both A and B to the same sample index.
- Fix camera, animation state, and history-reset frame.
- Keep every setting except the tested filter identical.
- Measure the same frame range in A and B.
- Separate warm-up frames from measurement frames.
- Preserve the two confirmed 1920x1080 DamagedHelmet ROIs unless a versioned replacement is explicitly recorded:
  - `rearward_surface`: x `895`, y `278`, width `75`, height `85`;
  - `underside_pipes`: x `805`, y `585`, width `125`, height `135`.

## Initial Implementation Audit

- `ReflectionEvaluatedRadiance` and `ReflectionResolvedRadiance` are persistent render-size `DXGI_FORMAT_R16G16B16A16_FLOAT` textures. Existing PNG capture converts the displayed output and is not a linear-HDR measurement path.
- Existing `DebugDumpCapture` demonstrates full-texture copy/readback and half-float decoding, but it is specialized for LightPass/back-buffer validation. Phase 1 should reuse the copy/readback pattern without changing the existing dump contract.
- `ReflectionRayHit` is `R16G16B16A16_FLOAT`: `.x` is committed hit distance, `.y` is the hit flag, and `.zw` is the octahedral hit normal. Hit rate and hit-distance statistics can be read without adding a new signal buffer.
- `ReflectionResolvedRadiance.a` currently carries diagnostic temporal status: `0.0` no history, `0.25` out of bounds, `0.5` depth reject, `0.75` normal reject, and `1.0` accepted. Phase 1 may consume this existing diagnostic metadata but must not redefine RGB radiance semantics or treat alpha as a production confidence contract.
- The first implementation slice should capture only fixed ROI texels from evaluated radiance, resolved radiance, ray hit, and temporal status. Avoid a new full-frame variance texture or denoise pass.

## Planned Gates

1. Implement one versioned 64-frame linear-HDR ROI capture and JSON result.
2. Prove paired sample/history reset and identical A/B frame windows.
3. Add the required metrics and High-SPP Current-Estimator Mean Baseline terminology.
4. Promote the same path to the 256-frame standard gate.
5. Run 1024 frames only if mean drift, rare fireflies, or bias trend remains unresolved.

