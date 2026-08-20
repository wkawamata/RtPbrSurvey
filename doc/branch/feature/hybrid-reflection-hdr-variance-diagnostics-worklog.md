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

## Implementation Log

### 2026-08-15: ROI HDR readback primitive

- Added a dedicated readback helper for `DXGI_FORMAT_R16G16B16A16_FLOAT` reflection signals.
- The helper validates and copies only the requested ROI and exposes decoded linear-HDR RGBA samples.
- Tone mapping and PNG conversion are deliberately outside this path.
- This slice does not yet schedule RenderGraph captures or emit statistics; those remain the next Phase 1 integration boundary.
- Debug x64 build, including HLSL custom build steps, succeeded. The build reported only the existing duplicate vcpkg import warning.

### 2026-08-15: RenderGraph capture integration

- Added an explicit one-frame engine request/result boundary for reflection HDR diagnostics.
- Added a diagnostic RenderGraph pass after scene rendering. It transitions and copies evaluated radiance, the current resolved-radiance write slot, and ray-hit payload from the same submitted frame.
- The request is rejected unless Deferred rendering and Hybrid Reflection contribution are active, preventing reads of signals that were not produced.
- Readback remains synchronous and intentionally limited to offline diagnostics. Continuous capture scheduling and JSON aggregation remain the next boundary.
- Debug x64 build succeeded with the existing duplicate vcpkg import warning only.

### 2026-08-15: Continuous frame scheduling and JSON smoke

- Added `-ReflectionHdrDiagnostics <report.json>` with warm-up, frame-count, and render-space ROI overrides.
- Diagnostic mode explicitly selects DamagedHelmet, Deferred rendering, and Hybrid Reflection contribution. It is mutually exclusive with screenshot capture automation.
- The schema records the linear-HDR domain, absence of a reference, render resolution, ROI, and per-frame evaluated/resolved mean luminance, hit rate, temporal acceptance rate, depth-reject rate, and normal-reject rate.
- A 64-frame run completed for `rearward_surface` at 1920x1080 after 32 warm-up frames. The report contained all 64 frames.
- The 64-frame run captured zero D3D12 errors and the same two known committed-buffer initial-state warnings.
- This validates collection continuity only. Temporal variance, distribution percentiles, paired A/B comparison, and baseline RMSE are not yet claimed.

### 2026-08-15: Pixel-temporal statistics

- Added signal-independent statistics for evaluated and resolved radiance.
- Temporal variance is the population variance around each pixel's own temporal mean, averaged over all ROI pixels. It does not confuse static spatial contrast with temporal noise.
- Frame-difference mean, p95, and p99 are calculated from the distribution of absolute luminance differences for every ROI pixel across consecutive frames.
- CV is emitted as `null` when the absolute temporal mean is at or below `1e-6`. Maximum luminance remains an auxiliary outlier indicator.
- A 64-frame `rearward_surface` run completed with finite statistics. In this run, evaluated variance was `8.16859365380723e-4` and resolved variance was `3.18587231219291e-5`.
- The observed evaluated/resolved difference is evidence that the measurement distinguishes their temporal behavior. It is not yet a paired filter comparison, baseline convergence result, or physical-correctness claim.
- Debug x64 build succeeded. The runtime captured zero D3D12 errors and the same two known warnings.

### 2026-08-15: Paired filter comparison

- Added sampling and temporal frame indices to each captured frame so paired scheduling can be verified from the reports.
- Added a filter off/on orchestrator. Each variant starts a fresh process, uses the same fixed camera, warm-up, measurement window, stochastic settings, temporal weight, and ROI, and differs only in `surfaceVarianceFilterEnabled`.
- The 64-frame `rearward_surface` development comparison matched sampling and temporal indices `32..95`. Evaluated mean luminance was identical between A and B.
- In this scoped run, resolved temporal variance decreased by `77.5619%`, while the resolved measurement-window mean differed by `0.1826%`.
- The result supports a development-level claim for this ROI and sequence only. It is not yet the 256-frame PR gate, a long-run mean-preservation claim, a second-ROI result, or a physical-reference comparison.

### 2026-08-15: Two-ROI 256-frame standard gate

- The `underside_pipes` 64-frame development gate matched sample indices and reduced resolved variance by `73.5387%`, with a `0.1029%` measurement-mean difference.
- Both fixed ROIs then completed the 256-frame standard paired gate with matching sampling and temporal index sequences and zero evaluated-mean difference.

| ROI | Resolved variance off | Resolved variance on | Reduction | Measurement-mean difference | Frame-difference p99 off/on |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | `3.8915666e-5` | `8.8329177e-6` | `77.3024%` | `0.1842%` | `0.0135398 / 0.0060411` |
| `underside_pipes` | `4.2294466e-5` | `1.0476135e-5` | `75.2305%` | `0.1021%` | `0.0077752 / 0.0034011` |

- These results support the scoped statement that the default-off surface filter reduces linear-HDR temporal variance for the two evaluated ROIs while changing the 256-frame measurement mean by less than `0.2%`.
- They do not establish generalization, a long-duration mean bound, estimator correctness, or physical-reference convergence. The 1024-frame audit remains conditional rather than automatic.

### 2026-08-15: Current-estimator mean baseline

- Added the High-SPP Current-Estimator Mean Baseline as the per-pixel arithmetic mean of `ReflectionEvaluatedRadiance` over the measurement window.
- The report records evaluated and resolved RMSE to that baseline, including per-frame RMSE series. The baseline is explicitly marked as non-physical and in-sample.
- Paired A/B baselines matched exactly because the evaluated signal and sampling sequence matched.

| ROI | Baseline samples | Evaluated RMSE | Resolved RMSE off | Resolved RMSE on | Filter-on RMSE change |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | 256 | `0.0284464` | `0.0062587` | `0.0132367` | `+111.4950%` |
| `underside_pipes` | 256 | `0.0285282` | `0.0065240` | `0.0068497` | `+4.9926%` |

- The filter reduced temporal variance by more than `75%` while increasing per-pixel RMSE to the raw current-estimator mean in both ROIs. The large rearward increase is consistent with a spatial-detail or local-mean change that an ROI-wide mean metric hides.
- This does not prove physical bias because the baseline is the current approximate estimator, not a physical reference. It does show that variance reduction and current-estimator signal preservation must remain separate gates.
- Do not promote the bounded surface filter based on variance reduction alone.

### 2026-08-21: Final audit

- The Japanese-primary [Phase 1 final report](hybrid-reflection-hdr-variance-diagnostics-report_j.md) consolidates the contract, two-ROI results, claim boundary, acceptance gates, and Phase 2 handoff.
- Final Debug x64/HLSL build succeeded.
- Final D3D12 smoke captured zero errors and the same two known committed-buffer initial-state warnings; no new warning was observed.
- The English and Japanese worklogs contain the same seven implementation phases and aligned results.
- The 1024-frame audit remains conditional and was not run.
