# Hybrid Reflection Stochastic Sampling Work Log

Japanese version: [Hybrid Reflection Stochastic Sampling Work Log (Japanese)](hybrid-reflection-stochastic-sampling-worklog_j.md).

This log records design decisions, implementation slices, and validation results for stochastic Hybrid Reflection sampling. Normative resource and temporal semantics remain in [Hybrid Reflection Contracts](hybrid-reflection-contracts.md).

## 2026-08-10: Phase Start and Current-Signal Audit

- Started `features/hybrid-reflection-stochastic-sampling` from `main` at `87b6f56`.
- Confirmed the current reflection ray direction is the deterministic perfect-mirror direction `reflect(viewDirection, normal)`.
- Confirmed visible-surface roughness currently gates ray generation through `maxRoughness` but does not broaden the ray direction.
- Confirmed hit-surface roughness remains a separate material payload in `ReflectionRayMaterial`; it must not drive the visible-surface sampling lobe.
- Confirmed `HybridReflectionPass` has no frame/sample index or random seed input.
- Fixed the initial implementation boundary: stochastic direction generation belongs in `HybridReflectionPass` before `RayQuery`; raw hit/material payload and evaluated/resolved radiance contracts remain unchanged.
- Production defaults must preserve the current deterministic image until stochastic sampling and temporal behavior pass validation.
- Temporal accumulation, spatial denoise, DLSS RR/Streamline integration, PathTracing, and broad RenderGraph changes remain outside this phase.
- Validation gate: compare deterministic and stochastic inputs at history weights `0.0` and `0.9`, checking temporal noise reduction, mean brightness, detail retention, motion trails, reversal, and settling.
- No renderer or shader behavior changed in this slice; build was not run.

## Planned Small Slices

1. Define the reflection-owned sample-index/reset contract and choose a minimal rough-specular sampling model.
2. Add default-off CPU/shader controls without changing resource semantics.
3. Implement stochastic visible-surface reflection directions and compile/build validation.
4. Extend the existing repeatable A/B suite only where required to measure the real stochastic signal.
5. Review whether the observed benefit justifies a nonzero production temporal default; do not enable it automatically.

## 2026-08-10: Sampling Model and Ownership Decision

- `ReflectionEvaluatePass` evaluates radiance arriving from the traced direction, while `LightPass` continues to own visible-surface distance, roughness, intensity, and Fresnel weighting.
- The current final composition is a deliberate reflection approximation, not a Monte Carlo BRDF estimator with an explicit PDF and throughput term. Adding a randomized direction alone must therefore not be described as unbiased path tracing or physically complete GGX integration.
- Use an isotropic GGX-derived rough-specular direction as the initial experimental model. The visible-surface roughness controls the sampling lobe; hit-surface roughness remains limited to hit-material radiance evaluation.
- Preserve the exact perfect-mirror direction when stochastic sampling is disabled or visible roughness is effectively zero.
- Reject sampled directions below the visible surface. The first implementation should choose a bounded deterministic fallback instead of tracing an invalid direction or introducing an unbounded resampling loop.
- Seed each sample from pixel coordinates and a reflection-owned sample index. The sequence must be reproducible for capture automation and must not depend on swap-chain back-buffer indices.
- A Hybrid Reflection signal execution advances the sampling index after submission. History invalidation also resets the sampling index so the first stochastic sample and temporal history restart together.
- Raw payload, evaluated radiance, and resolved radiance resource layouts remain unchanged. No PDF or throughput field is added to the payload in this phase.
- Expose stochastic sampling as a default-off experimental control. Changing its enable state or strength invalidates reflection history.
- Treat mean-brightness preservation as a measured acceptance gate, not as a property guaranteed by the initial approximation.

## 2026-08-11: Default-Off Control and Sample Index Wiring

- Added a default-off `Stochastic Rough Sampling` Debug UI setting without changing the ray direction yet.
- Added matching CPU pass and HLSL constants for the enable flag and reflection-owned sampling frame index.
- Expanded the Hybrid Reflection root constants from 9 to 11 DWORDs with matching CPU/HLSL field order.
- Added a sampling commit-pending flag. `HybridReflectionPass` records the pending advance, and the index advances only after the containing direct-queue command list is submitted.
- Reset the sampling index together with reflection history. Changing the stochastic enable state invalidates both so an experiment begins from sample zero with empty history.
- Kept sampling ownership independent of the swap-chain index and the Temporal Upscaler frame index.
- No stochastic direction calculation is present in this slice, so enabling the control still preserves the deterministic perfect-mirror ray.
- Validation: Debug x64 build and HLSL compilation succeeded with zero errors. MSBuild reported the existing duplicate vcpkg import warning; app-local deployment fell back from unavailable `pwsh.exe` to Windows PowerShell and completed.

## 2026-08-11: Rough-Specular Direction Implementation

- Added a shared `ReflectionSampling.hlsli` helper used by both `HybridReflectionPass` and `ReflectionEvaluatePass`.
- Generated one reproducible isotropic GGX-derived half-vector sample from pixel coordinates and the reflection-owned sampling frame index.
- Reflected the camera-to-surface direction around the sampled half vector. Roughness at or below `0.001`, a disabled experiment, or a below-surface result falls back to the exact mirror direction.
- Kept the fallback bounded to one sample and did not add a resampling loop.
- Reconstructed the identical sampled direction in `ReflectionEvaluatePass`. This keeps hit-surface view-dependent lighting and miss environment lookup consistent with the direction traced by `RayQuery` without expanding any payload resource.
- Stochastic misses use environment mip zero because visible-surface lobe broadening is supplied by the direction distribution. The deterministic path retains the existing roughness-prefiltered environment lookup.
- Added a two-DWORD pixel-shader root constant for the stochastic enable flag and current sampling frame index. No new texture, history, PDF, or throughput resource was introduced.
- The first build exposed a misplaced RenderGraph constant key in the pipeline-key struct. Moved it to the constants-key struct before committing.
- Validation: the corrected Debug x64 build and both HLSL compilations succeeded with zero errors. MSBuild retained the existing duplicate vcpkg import warning. An eight-second default-off DamagedHelmet run produced an empty D3D12 Debug Layer log; the generated log was removed.
- Visual validation with stochastic sampling enabled remains pending.

## 2026-08-11: Automated Stochastic Capture Control

- Added `-ReflectionStochasticSampling` as a flag for resolved-radiance capture automation.
- The flag sets the existing default-off Hybrid Reflection setting after DamagedHelmet auto-selection; it does not change interactive or production defaults.
- Kept temporal history weight and synthetic noise as separate CLI controls so the next A/B run can enable the same real stochastic input in both variants and vary only history weight.
- The first build attempt ran after another local task switched the shared workspace to its own branch. Path-limited stash/switch/pop restored only this task's three CLI edits to the intended branch without touching the other task's committed or untracked files.
- Validation: Debug x64 build succeeded with zero errors. An eight-second DamagedHelmet resolved-radiance run with stochastic sampling enabled and history weight zero produced an empty D3D12 Debug Layer log; the generated log was removed.

## 2026-08-11: Real-Signal Subjective Suite

- Added `-StochasticSampling` to the existing validation runner while preserving its original synthetic-noise mode.
- Added dedicated `suite-stochastic.json` and `capture-plan-stochastic.json` inputs. The plan retains the same warm-up, mid-motion, reversal, and settling timeline but uses distinct suite/case identifiers and `stochastic-*.png` paths so prior contract captures are not overwritten.
- Both stochastic variants enable real rough-reflection sampling and set synthetic noise to zero. A uses history weight `0.0`; B uses `0.9`.
- Kept nine focused criteria covering real-noise reduction, tracking/trails, disocclusion edges, mean brightness, stable detail, and settling. Added complete Japanese localization alongside English.
- Static validation confirmed all three plan cases map exactly to suite cases and A/B paths, and the PowerShell runner parses without errors.
- An initial six-image capture completed successfully and all PNGs loaded at 1920 by 1080 in the local evaluator. The suite is committed before the report-bearing recapture so generated metadata can identify a clean source revision.

### Formal Subjective Result

- Recaptured all six images from commit `5092138` with `workingTreeDirty: false`.
- Capture conditions were stochastic sampling enabled, synthetic noise `0.0`, A history weight `0.0`, and B history weight `0.9`.
- The user marked all nine criteria as `pass`: noise, tracking, brightness, reversal trail, disocclusion/screen edges, settling, and stable detail.
- No defect tags or notes were recorded. The saved report locale was English.
- The generated report remains a local ignored artifact so the repeatable HTML harness, stable suite, and capture plan stay separate from individual judgments.
- Decision: the sampled-frame real-signal gate passes. This result does not yet measure between-capture flicker, long-run estimator bias, or live motion quality, so it does not by itself authorize a nonzero production history default.

## 2026-08-11: Live Motion A/B Result

- Added and ran the committed `capture-plan-stochastic-live.json` timeline: initial hold, three slow orbit segments with two direction changes, return to the initial yaw, and a final settling hold.
- A used stochastic sampling with history weight `0.0`. The user observed unstable regions even while stationary, unstable edges and internal reflections during motion, continued instability after direction reversal, and no convergence to a stable image after stopping.
- B used the identical timeline with history weight `0.9`. The user observed reduced grain/flicker, stable edges and internal reflections overall, no perceptible reversal trail or delay, and acceptable settling without an old-reflection remnant.
- B retained minor edge flicker during motion. Mean brightness and detail were judged to have no apparent unnatural loss.
- Decision: history weight `0.9` provides a clear live stability benefit for the current one-sample stochastic input and passes this DamagedHelmet live-motion gate with a minor residual moving-edge issue.
- Keep the global production defaults at stochastic sampling disabled and history weight `0.0`. The evidence supports a future explicit stochastic-temporal preset, but one scene, one nonzero weight, and the current approximate estimator are not sufficient to silently change the global default.
- Spatial denoise and broader scene coverage remain separate follow-up work; they are not added to this branch in response to the residual edge flicker.

## 2026-08-11: Phase Closeout

- Updated the normative Hybrid Reflection contract with the implemented direction-sampling boundary, sample-index ownership, deterministic fallback, Evaluate direction reproduction, conditional miss environment lookup, and approximation limitation.
- Updated the foundation note only where its current-status and historical root-constant descriptions had become stale; detailed normative semantics remain in the focused contract.
- Confirmed raw payload and evaluated/resolved radiance layouts remain unchanged, and no PDF, throughput, denoise, confidence, or additional history resource was introduced.
- Final defaults remain stochastic sampling disabled, temporal history weight `0.0`, and temporal debug noise `0.0`.
- Validation at closeout consists of the successful Debug x64 build and HLSL compilation, empty D3D12 Debug Layer runs, the clean-commit six-image suite with nine passes and no defects, and the live A/B result recorded above.
- The residual moving-edge flicker, broader scene/roughness coverage, alternative history weights, long-run mean-bias measurement, and any spatial denoise are explicit follow-up candidates rather than unfinished work in this branch.
