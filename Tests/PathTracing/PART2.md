# Static HDR convergence measurement

Run Part 1 first to check fixture loading, radiance mode, dimensions and captures.
The suite uses ordinary editable scene files without changing source assets or the estimator.
All source text is CRLF UTF-8 without BOM. Captures, generated variants, plots and raw reports remain ignored under bin.

## Protocol selected after smoke

The two 8 spp smoke captures took about 13 seconds each on RTX 3080 Laptop GPU, driver 616.64.
Before inspecting convergence results, select 8/32/128 spp, three evaluation seeds 1/2/3,
and a common 512 spp reference averaged from independent seeds 101/102.
Constant-environment modes are 1=BSDF, 2=uniform NEE, 5=MIS. Both scenes use two bounces,
one sample/frame, unit constant environment, no local lights/emission, Russian roulette off,
static saved camera, shadow rays enabled and fixed exposure. All captures are 1920x1080.
Accumulation starts fresh for every process; capture triggers at the exact target sample count.

ROIs use top-left coordinates and avoid silhouettes/background:

| Fixture | ROI name | x,y,width,height |
| --- | --- | --- |
| constant-environment | floor | 928,508,64,64 |
| roughness | roughness-0.18 | 664,424,48,48 |
| roughness | roughness-0.4 | 936,424,48,48 |
| roughness | roughness-0.8 | 1208,424,48,48 |

Each mode/sample group uses the same reference for its scene/ROI. PFM is already normalized
by per-pixel accumulated samples by the renderer; the Python reader must not divide again.
It checks the entire PFM for non-finite values and converts bottom-up rows before cropping.

The suite records per-seed RGB RMSE, mean-image RGB RMSE, average unbiased sample variance
across seed images, mean RGB and seed counts. Reference pairwise RMSE is reported separately.
The reported `meanPixelStandardError` is the **RMS pixel-channel standard error** of the reference mean,
computed as sqrt(average unbiased seed variance / reference seed count); it is not a confidence interval
or the standard error of the spatially averaged RGB. No spatial independence is assumed.

Fixed-seed repeats compare complete PFM hashes. Process failure, timeout, wrong sample/seed/mode/bounces/
resolution, missing capture, non-finite values and D3D12 errors fail the run and remain in the incremental JSON.
Nonempty output directories are rejected, preserving failed attempts. A measured convergence trend is
an observation, not a pass/fail threshold to relax. A noisy or flat curve may be limited by reference uncertainty.

## Separate direct/indirect controls

`--lighting-controls` uses the same red-wall/floor fixture with derived one-bounce direct-only
and four-bounce indirect presets, environment and emission off, local point light enabled, mode 0.
Each cohort has its own 512 spp reference; sample-count convergence is assessed within that fixed-bounce cohort.
Comparing one and four bounces describes changed path contributions and must not be called a convergence
improvement. This also exercises a deterministic direct-light estimator: zero seed variance is valid there.
The primary ray is still jittered within the pixel, so a small nonzero variance is expected for a spatially
varying direct-lit plane; the deterministic statement applies to summing the lights at a fixed surface hit.

## Commands

Python 3.10+ is sufficient for capture/statistics; only plotting requires matplotlib.

```powershell
python -B Tests/PathTracing/test_compare_hdr.py
python -B Tests/PathTracing/run_convergence_suite.py --smoke --samples 8 --output bin/PathTracingValidation/part2-smoke-new
python -B Tests/PathTracing/run_convergence_suite.py --output bin/PathTracingValidation/part2-main-new
python -B Tests/PathTracing/run_convergence_suite.py --lighting-controls --output bin/PathTracingValidation/part2-controls-new
python -B Tests/PathTracing/compare_convergence.py --suite-report bin/PathTracingValidation/part2-main-new/report.json --output bin/PathTracingValidation/part2-main-new/plots
python -B Tests/PathTracing/compare_convergence.py --suite-report bin/PathTracingValidation/part2-controls-new/report.json --output bin/PathTracingValidation/part2-controls-new/plots
```

The original two-report, direct-only `compare_convergence.py` CLI remains supported.
`compare_hdr.py` additionally accepts constant modes 1/2/5, keeping existing map-mode defaults.
Use separate output directories and avoid simultaneous capture runs when reproducing.
PNG/SVG charts are standalone plotting artifacts, not copies of the rendered scene images.
The generated Markdown contains a compact metric table; the final repository report interprets the results.

This experiment uses one GPU and three small fixtures. It does not validate moving cameras, other GPU drivers,
arbitrary environments, pure Lambert materials, absolute estimator bias or general sampler performance.
Equal spp is not equal GPU work: MIS evaluates both sampling techniques. These error/variance curves
are not a performance ranking; GPU cost measurement belongs to Part 4.
The reference uses the same implementation with independent seeds; agreement cannot rule out shared bias.
