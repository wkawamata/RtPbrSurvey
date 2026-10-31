# RELAX known-noise controls

## Predeclared experiment (2026-10-20)

Purpose: measure the response of the actual pinned RELAX GPU path to known
radiance variance, independently of the PT sampler and scene material split.
This is Step 1 of bug/input-quality/filter-behavior separation, not a general
denoiser quality guarantee or completion of the scene investigation.

Keep the existing 64x64 static-plane guides, distance 2, normal encoding 4,
roughness 0.5, zero motion, matrices, 30/50 pre-pass radii and default history
settings unchanged. No material restoration is involved. Inputs are RGBA32_FLOAT
and outputs RGBA16_FLOAT. Fresh backend per seed/amplitude, reset at SDK frame 0.
CPU/GPU fences complete before upload memory or frame resources are reused.

- Seeds: 7, 101, 307.
- Multiplicative amplitudes: 0, 0.25, 0.75.
- Constant truth: diffuse RGB (0.125,0.5,2), specular RGB (2,0.25,0.125).
- Elapsed checkpoints: 1, 30, 120; SDK indices 0,29,119.
- Expected inventory: 27 checkpoints, 54 lobe assessments.
- Adjacent horizontal pixel pairs receive opposite signs. Signs vary by seed,
  SDK frame, pair, lobe and RGB channel through a fixed 32-bit integer hash.
- Input is nonnegative without clamping and has exactly the constant spatial
  mean in every frame. This pairing creates spatial correlation: it is NOT
  independent white noise, blue noise or a representative PT noise model.

The C++ runner uploads each new input frame, executes actual SDK dispatches,
and records full output and current-input GPU readback plus CPU temporal input
means. Python independently regenerates every input in each checkpoint's entire
history, verifies exact input/readback/temporal-mean equality, and recomputes
the reported metrics. No pass flag or case count alone is sufficient evidence.

## Criteria fixed before acquisition

Execution requires exact case inventory, matching runtime/configuration,
positive dispatch counts, finite nonnegative signals and no D3D12 errors.
Missing, duplicated, forged or mismatched evidence is an execution failure.

Amplitude 0 must exactly reproduce the freshly rerun constant control output
and retain its per-channel bound: `0.004 * max(1, expected)` for these nonzero
channels. The separate zero-signal controls retain the previous `1e-6` bound.

For nonzero amplitudes, checkpoints 30/120 use the application study criteria:
mean RGB vector bias <= 5% and output NRMSE <= current input NRMSE relative to
the known constant truth. Checkpoint 1 is descriptive, not a warm-up quality
gate. Overall: 42 evaluated lobe cases, 12 descriptive lobe cases. These are
study acceptance goals, not SDK guarantees; a numeric failure is not an
execution failure or automatic proof of an integration bug.

Also report temporal Native NRMSE, calculated from the same per-pixel inputs
over all elapsed frames. Beating that value is not mandatory, and this is not
an equal-work comparison: NRD uses spatial neighbors and capped temporal
history, while Native averages a growing per-pixel history.

No thresholds, masks or SDK settings may be changed after inspecting results.
Step 2 remains responsible for relating these controlled results to the actual
glossy scene, including guides, estimator variance and per-lobe ground truth.
