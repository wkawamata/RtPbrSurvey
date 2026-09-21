# Path Tracing Reference Capture

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
