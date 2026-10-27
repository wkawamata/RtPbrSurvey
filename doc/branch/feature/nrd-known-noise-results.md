# RELAX known-noise results

## Step 1 result (2026-10-20)

Execution: `completed-known-noise-study`.
Quality: `passed-known-noise-cases`, using the predeclared criteria in
`nrd-known-noise-controls.md`, not an SDK guarantee or general scene verdict.

Local accepted evidence:

- Baseline: `bin/PathTracingValidation/nrd-noise-baseline-20261020/report.json`.
- Noise study: `bin/PathTracingValidation/nrd-known-noise-20261020/report.json`.
- Full GPU readback: `gpu-report.json` in each folder.

The executable reran all four constant controls before the noise study. The
noise study contains 27 checkpoints and 54 lobe assessments: 18 zero-amplitude
controls, 24 evaluated noisy warm-up cases, and 12 descriptive first-frame
noisy cases. All 42 evaluated cases passed. Each zero-amplitude output is
bit-identical to the fresh constant baseline, including the input-update path.

Python regenerated each seed/frame/lobe/channel input sequence. Current GPU
input readbacks at checkpoints exactly matched it, with exact spatial means.
CPU temporal averages for the full preceding sequence also matched exactly.
Intermediate-frame uploads use the same GPU copy path; only checkpoint inputs
and outputs are read back, not every intermediate input. The manifest, all
27 SDK shaders, executable, sources/specification and raw evidence hashes are
recorded. No threshold or SDK setting was retuned.

## Measurements

Ranges cover all three seeds. NRMSE and bias below are percentages relative to
the known constant truth, using RGB vector normalization. Each current input's
NRMSE is the declared amplitude (25% or 75%) up to floating-point math.

| Noise | Frames | Lobe | Output NRMSE | Maximum mean RGB bias | Temporal Native NRMSE |
| --- | --- | --- | --- | --- | --- |
| 25% | 30 | Diffuse | 0.6588-0.6629 | 0.6624 | 4.5181-4.6377 |
| 25% | 30 | Specular | 0.6880-0.6918 | 0.6915 | 4.4801-4.5907 |
| 25% | 120 | Diffuse | 1.0147-1.0205 | 1.0204 | 2.2441-2.3320 |
| 25% | 120 | Specular | 1.0268-1.0286 | 1.0286 | 2.2699-2.3185 |
| 75% | 30 | Diffuse | 0.6853-0.7065 | 0.7058 | 13.5542-13.9130 |
| 75% | 30 | Specular | 0.7305-0.7463 | 0.7449 | 13.4404-13.7721 |
| 75% | 120 | Diffuse | 1.1359-1.1489 | 1.1486 | 6.7324-6.9959 |
| 75% | 120 | Specular | 1.1495-1.1592 | 1.1589 | 6.8098-6.9556 |

All 36 nonzero-noise lobe cases (including frame 1) had all three output mean
channels below truth. Bias increased between the measured 30/120 checkpoints.
This is reported, not hidden by the pass verdict. The outputs beat temporal
Native in these 36 cases, but paired spatial noise is particularly amenable to
spatial filtering. This is not an equal-work or real-PT comparison. The default
capped NRD history and growing Native accumulation remain distinct.

## Verification and interpretation

- Hardware D3D12 execution, actual RELAX dispatches; D3D12 errors: 0.
- Debug target build: 0 warnings / 0 errors.
- NRD Python regression tests: 163/163 passed.
- Related SDK/CLI CTests: 2/2 passed.
- Historical and alternate app hashes unchanged; only the standalone diagnostic
  executable was rebuilt. Native transport/defaults are unchanged.

The tested adapter can denoise known nonnegative radiance with coherent static
guides, and shows a measurable small downward difference despite unbiased
spatial input means. This supports separating denoising bias from PT estimator
or composition bugs rather than treating every scene threshold exceedance as
an integration defect. It does NOT prove the source of the difference inside
the SDK, establish a universal NRD performance ceiling, or rule out errors in
actual scene guides, material factors, hit distances or resource integration.

Step 1 is complete for its predeclared scope. Step 2 should return to the glossy
fixture and compare validated low-noise inputs or converged per-lobe references
before attributing its remaining bias. Do not bypass the app's 1-spp gate or
promote diffuse radius 5/default-on based on this synthetic experiment.
