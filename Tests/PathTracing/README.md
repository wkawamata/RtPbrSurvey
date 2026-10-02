# Path Tracing Reference Capture

Editable analytic fixtures and the Part 1 runner are described in [PART1.md](PART1.md).
The static multi-sample, multi-seed convergence suite is described in [PART2.md](PART2.md).
Scene-scale, contact-shadow and self-intersection measurements are described in [PART3.md](PART3.md).
GPU pass performance measurements are described in [PART4.md](PART4.md).

`Invoke-ReferenceCapture.ps1` runs two fixed-sample Path Tracing captures and writes JSON and Markdown reports.
It fails when the PNG hashes differ, a process fails, a capture times out, or a D3D12 error/corruption message is logged.

From the repository root:

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -SceneName DamagedHelmet `
  -Samples 64 `
  -Seed 1
```

To restore a saved Evaluation Case, including its scene, camera, rendering settings, ROI, Japanese comments, and test
items:

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -EvaluationCaseName "PT Normal Map" `
  -Samples 64 `
  -Seed 1
```

Generated captures, logs, and reports are written under `bin/x64/Debug/PathTracingReference` by default and must not
be committed.

Environment sampling is selected with `-EnvironmentMode` (or the application flag
`-PathTracingEnvironmentMode`): 0 = map BSDF, 1 = constant BSDF, 2 = constant NEE,
3 = map uniform NEE, 4 = map importance NEE, 5 = constant MIS, 6 = map uniform MIS,
7 = map importance MIS. Use separate output directories when comparing modes.
Modes 3/4 use the actual environment map for lighting; primary skybox visibility remains independent.

```powershell
.\Tests\PathTracing\Test-ConstantEnvironmentPdf.ps1
.\Tests\PathTracing\Test-EnvironmentImportancePdf.ps1
.\Tests\PathTracing\Test-EnvironmentMis.ps1
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 -EnvironmentMode 3 -Samples 256 -OutputDirectory bin/PT-Uniform
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 -EnvironmentMode 4 -Samples 256 -OutputDirectory bin/PT-Importance
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 -EnvironmentMode 7 -Samples 256 -OutputDirectory bin/PT-Importance-MIS
```

The PDF scripts check analytic estimator contracts on the CPU. They do not execute the shader and do not
establish HDR image convergence. Importance NEE uses a coarse equal-solid-angle distribution with a 5% uniform
mixture. MIS modes combine one environment sample and one BSDF sample using power-heuristic weights.
With shadow rays disabled, MIS modes fall back to environment NEE only, because unoccluded NEE and
occluded BSDF escape rays do not represent the same visibility integral.

## Linear HDR comparison

With Path Tracing active, `-CapturePath result.pfm` saves the float32 accumulation buffer divided by its
per-pixel sample count. PFM contains bottom-up RGB rows, little-endian floats, unit scale, before ToneMap.
PNG capture behavior is unchanged. Non-finite radiance or invalid sample counts fail the PFM capture.

Python 3.10+ (standard library only):

```powershell
python -B Tests/PathTracing/test_compare_hdr.py
python -B Tests/PathTracing/compare_hdr.py --output bin/PT-HdrComparison
```

Default comparison: DamagedHelmet with scene defaults, ROI `(885,460,175,180)` in top-left image coordinates,
64 samples, seeds 1/2/3, modes 0/3/4/6/7. The reference is the mean of mode 7 at 1024 samples with independent
seeds 101/102. Change `--roi`, `--scene`, `--samples`, `--reference-samples`, `--seeds`, and `--modes` explicitly
for other conditions. At least two distinct seeds are required for each group and groups must not overlap.

The JSON report contains per-seed RGB RMSE, RMSE of each mode's mean image, mean unbiased sample variance
across seeds, mean RGB radiance, capture hashes, settings diagnostics, and reference disagreement RMSE.
The Markdown report summarizes these metrics. Reference disagreement is not a statistical confidence bound;
the finite-sample MIS reference is not ground truth. The report measures total path radiance including direct
light and emission, not an isolated environment-only signal. It reports evidence without asserting which
technique must win. It fails on non-finite HDR, mismatched sample/mode/seed/dimensions, D3D12 errors, process
failure, timeout, or a failed fixed-seed repeat. Generated PFM/log/report files remain under `bin/`.
For a stochastic scene such as DamagedHelmet, add `--require-seed-variation` to reject runs whose variance
is effectively zero in every mode. Do not use this assertion for a deliberately constant/black ROI.

The Step 6 RNG separates hashing of the sample index and seed. The former `sampleIndex ^ seed` mapping
permuted the same sample set for power-of-two sample counts and small seeds, invalidating independent-seed
statistics. Old PNG hashes change with this correction; fixed-seed repeatability remains required.

## Multi-light direct estimator

`Test-MultiLightAdditivity.ps1` captures the bundled four-light scene as zero lights, each light alone, and all
lights together. It disables environment/emissive terms and uses one bounce so the test isolates direct lighting.
At identical seed and sample count, it compares linear HDR `all - none` with the sum of each `single - none` on
an 8-pixel grid across the full image. It checks that every light contributes, the capture diagnostics match the
requested settings, and the D3D12 log contains no error. The generated presets, PFM files, logs, and JSON report
are under `bin/x64/Debug/PathTracingMultiLightAdditivity` and must not be committed.

```powershell
.\Tests\PathTracing\Test-MultiLightAdditivity.ps1 -Samples 1 -Seed 1
```

This checks the all-light summation path, not Point/Spot attenuation formulas, visibility edge cases, or
multi-seed convergence. Those require separate tests.

`Test-MultiLightRangeCone.ps1` captures the same scene with a Point or Spot light active, with range reduced to
0.1, and with the Spot aimed upward. The short-range and upward cases must match the zero-light PFM byte for
byte; the normal Point and Spot cases must differ from zero lights. It uses the same direct-only, one-bounce
settings, checks capture diagnostics and D3D12 errors, and writes ignored artifacts under
`bin/x64/Debug/PathTracingMultiLightRangeCone`.

```powershell
.\Tests\PathTracing\Test-MultiLightRangeCone.ps1 -Samples 1 -Seed 1
```

This verifies the range and cone exclusion behavior on the bundled fixture. It does not prove the inverse-square
falloff or cone interpolation numerically, or whether an occluder beyond the light is excluded by the shadow ray.

`Test-LocalLightVisibility.ps1` generates a direct-only floor scene with a Point or Spot light. It compares the
center floor ROI with no blocker, a blocker between the floor and light, and a blocker beyond the light. The
between case must darken the floor; the beyond case must reproduce the unblocked HDR values in the ROI. The
bundled spheres remain in the generated scene but are moved out of view. Scene variants, PFM captures, logs,
and JSON reports are generated under `bin/x64/Debug/PathTracingPointVisibility` or
`bin/x64/Debug/PathTracingSpotVisibility` and must not be committed.

```powershell
.\Tests\PathTracing\Test-LocalLightVisibility.ps1 -LightType Point -Samples 1 -Seed 1
.\Tests\PathTracing\Test-LocalLightVisibility.ps1 -LightType Spot -Samples 1 -Seed 1
```

The ROI comparison isolates visible floor pixels; it does not require the entire images to match because the
blocker itself may be visible elsewhere. It checks finite shadow distance, not falloff magnitude or penumbrae.

`Test-PointLightFalloff.ps1` checks the Point light's numerical attenuation on the same floor with no blocker,
environment, or emissive lighting. It captures a light directly above the center ROI at heights 2 and 4 with
range 20, then at height 2 with range 5. The measured linear HDR ratios are compared with
`(1 - (distance / range)^4)^2 / distance^2` at the ROI center. A 2% relative tolerance allows the small changes
in light angle and BRDF across the sampled 16x16-pixel ROI. It checks settings diagnostics and D3D12 errors.
Generated scenes, presets, PFM files, logs, and the JSON report remain under
`bin/x64/Debug/PathTracingPointFalloff` and must not be committed.

```powershell
.\Tests\PathTracing\Test-PointLightFalloff.ps1 -Samples 4 -Seed 7
```

This is an approximate output-level ratio check, not an analytic reference for every floor pixel or a test of
the Spot cone interpolation.

## Multi-light convergence

`compare_hdr.py` accepts `--scene-file` and `--render-preset` for the bundled multi-light fixture. With
`--direct-only`, it writes a derived preset under the output directory with environment/emissive disabled and
one bounce; the source preset is unchanged. `--reference-mode 0 --modes 0` compares the direct-light estimator
only. Run two sample counts with disjoint evaluation/reference seeds, then use `compare_convergence.py` to
recompute both against the same higher-sample reference. The reports include scene/preset hashes and capture
diagnostics; generated PFM/log/report files under `bin/` must not be committed.

```powershell
python -B Tests/PathTracing/compare_hdr.py --scene-file Assets/Scenes/MultiLightValidation/scene.json --render-preset Assets/Scenes/MultiLightValidation/render-preset.json --direct-only --roi 600 300 720 480 --samples 8 --reference-samples 32 --seeds 1 2 3 --reference-seeds 101 102 --modes 0 --reference-mode 0 --require-seed-variation --output bin/PathTracingMultiLightConvergence-8spp
python -B Tests/PathTracing/compare_hdr.py --scene-file Assets/Scenes/MultiLightValidation/scene.json --render-preset Assets/Scenes/MultiLightValidation/render-preset.json --direct-only --roi 600 300 720 480 --samples 32 --reference-samples 128 --seeds 1 2 3 --reference-seeds 101 102 --modes 0 --reference-mode 0 --require-seed-variation --output bin/PathTracingMultiLightConvergence-32spp
python -B Tests/PathTracing/compare_convergence.py --low-report bin/PathTracingMultiLightConvergence-8spp/report.json --high-report bin/PathTracingMultiLightConvergence-32spp/report.json --output bin/PathTracingMultiLightConvergence-common/report.json
```

On RTX 2080 Ti (2026-09-27), the common 128 spp reference gave mean-image RGB RMSE `0.04663` at 8 spp and
`0.02142` at 32 spp; seed variance was `0.007053` and `0.002356`, respectively. Fixed-seed repeats matched,
and all captures had zero D3D12 errors. The two 128 spp reference seeds disagreed by RMSE `0.03143`, so this
is evidence of a convergence trend, not a ground-truth error or bias estimate.

Commit 8 exposes these current-frame primary-surface resources through RenderGraph and Debug Texture Preview:

- `PathTracing.NormalRoughness`
- `PathTracing.ViewZ`
- `PathTracing.MotionVectors`
- `PathTracing.Albedo`

Use `-DebugPreviewResource <name>` with the normal Path Tracing CLI arguments to open one for visual validation.
Motion-vector previews use a centered 32x display scale by default, so zero motion remains neutral gray while small
positive and negative values remain visible. Run the stationary/camera-motion comparison with:

```powershell
.\Tests\PathTracing\Invoke-MotionVectorValidation.ps1
```

The generated captures and report are written under `bin/x64/Debug/PathTracingMotionVectorValidation` by default and
must not be committed.
