# Path Tracer correctness completion: Steps 1-3

Date: 2026-10-03. Workspace: `C:\work\RtPbrSurvey-work-3`.
Branch: `codex/pt-correctness-completion`.
Renderer base: `0e6fd04125281400d492991c2cc740b7806e9e34` (PRs #85, #86, #87 included).

This work validates the estimator before adding denoising or caching. No PT shader, material,
camera, or rendering settings implementation is changed by the new measurement tools.
Source fixtures are unchanged; derived scenes, raw captures, and logs remain in ignored `bin/`.
Tests and reports were created in the working tree after the renderer base was built.

## Step 1: regression after fixes

- [x] Debug x64 MSBuild succeeded. One existing vcpkg MSB4011 import warning; zero errors.
- [x] CTest: 24/24 passed after building three previously missing test executables.
- [x] Python unit tests: 51/51 passed, including numeric rejection, uncertainty, policy completeness,
  supplementary failure propagation, and automatic control selection.
- [x] Part 1: 13 HDR / four PNG captures at 8 spp; fixed-seed repeat, seed variation,
  finite visibility, and attenuation passed. Attenuation ratio error: 0.18019%.
- [x] Part 2: reduced convergence regression, 22 captures, 4/16 spp, 32 spp common finite reference,
  evaluation seeds 1/2 and reference seeds 101/102, constant BSDF/MIS modes 1/5.
  All eight series reduced mean-image RGB RMSE and seed variance. This is not a full 512 spp reference rerun.
- [x] Part 3: full 57-capture main scale campaign. Supplementary TMax/near-light runs are recorded below.
- [x] Part 4: three-case GPU timing smoke, eight warm-up / 16 measurement observations, one repeat.
  Correct images and GPU timestamp availability were verified. The baseline timing was unstable
  (median 0.477 ms, p95 23.448 ms), so these values are not accepted as a performance baseline.
  The follow-up baseline-only run used 64 warm-up / 64 measurement observations and three repeats:
  medians 0.47296 / 0.479232 / 0.48064 ms; p95 0.47716 / 0.482534 / 0.485211 ms.
  Longer warm-up produced stable measurements. This does not establish a general cross-resolution/GPU ranking.
- [x] Part 5: all 20 standard native-buffer cases passed. Two ViewZ follow-up controls are recorded below.

GPU: NVIDIA GeForce RTX 2080 Ti, driver 616.56. Previous reports were on RTX 3080 Laptop GPU,
driver 616.64, before the material/ViewZ fixes. Their numeric differences do not isolate GPU effects.
A same-source, same-protocol rerun on that GPU remains a separate cross-GPU verification.

Local artifacts: `bin/PathTracingValidation/completion-20261003/part1/` and
`bin/PathTracingValidation/completion-20261003/suite/`, plus `controls/` and `performance-repeat/`
under the same dated folder. The compact committed evidence is
`path-tracing-validation-results/completion-steps-1-3-summary.json`.

## Step 2: indirect transport, GGX, Russian Roulette

- [x] Added `validate_transport.py` with metallic 0/1, roughness 0.18/0.4/0.8.
- [x] Compare BSDF/MIS in linear HDR against CPU BRDF hemisphere quadrature.
  Orders 128/256 and nine representative view directions check quadrature/spatial approximation.
- [x] Added a six-wall room with one/two/eight-bounce controls and RR off/on at eight bounces.
  Contributions beyond two bounces must be nonzero; otherwise RR-eligible paths were not established.
- [x] Complete the 64 spp / four-seed GPU campaign: 64 captures, 19 comparisons passed.

Evaluation seeds: 11, 23, 37, 53. Constant-environment ROI: (928,508,64,64).
The maximum BSDF/MIS mean difference was 0.10144%; maximum GPU/quadrature difference was 0.05865%.
The nine-view-direction CPU quadrature changed by less than the predeclared 0.2% bound at every material.

| Material | Roughness | BSDF/MIS difference | BSDF/quadrature difference | MIS/quadrature difference |
| --- | ---: | ---: | ---: | ---: |
| Dielectric | 0.18 | 0.00200% | 0.01791% | 0.01991% |
| Dielectric | 0.4 | 0.04432% | 0.01836% | 0.02597% |
| Dielectric | 0.8 | 0.00868% | 0.01709% | 0.00840% |
| Metal | 0.18 | 0.02928% | 0.01545% | 0.01384% |
| Metal | 0.4 | 0.10143% | 0.05865% | 0.04284% |
| Metal | 0.8 | 0.00347% | 0.01433% | 0.01780% |

Enclosed-room ROI: (928,508,64,64). Mean RGB-channel radiance at one/two/eight bounces was
0.0486494 / 0.0984926 / 0.203123. The beyond-two-bounce contribution was 0.104630.
At eight bounces, RR ON mean was 0.202861; relative difference from OFF was 0.12911%,
with a paired four-standard-error band of 0.23802%. The fixed-limit mean-preservation check passed.
This does not prove convergence of the infinite-bounce solution or correctness for every RR/material combination.

Agreement permits 2% discrepancy plus four seed-level standard errors. Uncertainty above 5%,
or quadrature change above 0.2%, is inconclusive. This is a diagnostic band, not a formal confidence
interval or a proof of unbiasedness. Both methods share the material/camera model; the independent
quadrature checks integration, not those parsers. Single-scattering GGX is not claimed to preserve
all energy at high roughness. The fixed bounce limit is the same for RR off/on.

## Step 3: self-intersection policy

- [x] Added `assess_scale_policy.py` to separate supported relative settings from negative controls.
- [x] Finalize the policy from the new visibility/contact/convex-surface measurements.

All six relative-policy visibility cases passed. Relative contact shadows contained 124 shadowed columns
  at each scale. Relative convex-surface ratios were 1.0 with zero darkened channels.
Fixed bias at scale 0.01 / angled light had blocked/clear ratio 0.805391 (outside the 0.8 limit).
Its contact shadow had zero shadowed columns; changing only TMin did not fix it, while scaling bias did.
Zero-offset convex controls retained only 41.2%-45.5% of unshadowed radiance, with 95.4%-96.5% darkened channels.

Decision: keep explicit world-unit controls, use the validated proportional settings for this scaled fixture
family, and do not advertise fixed 0.01/0.001 offsets as scale-independent. A zero-offset policy is rejected.
An automatic scene-extent/bit-offset implementation is deferred until translated-coordinate and thin-surface
fixtures establish a requirement; it must not conceal this small-scale contact-shadow failure.

Supplementary validation: six TMax captures and twelve near-light captures completed without failures.
Point/Spot, each at X offset 0/1, had zero between-light radiance and zero beyond-light difference.
Short normalized TMax=2 clipped primary geometry to background; TMax=20 restored the receiver.
This is a shared primary/continuation/shadow distance control, not an independently limited shadow distance.
The moving shifted-projection and transformed-camera ViewZ controls also passed; maximum absolute errors
were 0.00001414 and 0.00004685, respectively, below 0.0001.

Total: 206 application captures (180 PFM, 22 PTBUF, four PNG). A separate derived diagnostic PNG
was generated from a performance PFM. The log scan found 476 existing InitialState warning lines,
zero unexpected warnings, and zero ERROR/CORRUPTION lines.

## Limits and follow-up

No non-finite HDR, process failure, timeout, or D3D12 ERROR/CORRUPTION occurred in the validation campaign.
Existing buffer InitialState warnings remain in the raw logs. No subjective judgment or manual interaction
was required for these numeric checks. Tests of animated-object motion vectors, extreme smooth/grazing GGX,
different RR material families, thin/grazing/translated-coordinate self-intersection, and a same-source
second-GPU campaign remain outside the measured domain.

`normalBias`, `rayTMin`, and `rayTMax` are world-distance quantities, not normalized percentages.
The tested relative convention is `normalBias=0.01*s`, `rayTMin=0.001*s`, `rayTMax=10000*s` for
the fixture scale `s`. It is not an automatic scene-bounding-box heuristic. Blindly multiplying
by a large scene extent can hide thin geometry, so no automatic origin-offset change is introduced here.

The assessment requires all six relative visibility cases, contact shadows at all three scales,
and convex-surface shadow/no-shadow ratios within 1% with at most 1% darkened channels.
Fixed and zero-offset controls remain visible in the report, not silently relabeled as passes.
Thin geometry, grazing rays, nonuniform scaling, very large translated coordinates, and complex
imported meshes remain outside this fixture-family guarantee.

## Reproduction

```powershell
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/run_completion_regression.py --output bin/PathTracingValidation/completion-reproduction
python -B Tests/PathTracing/assess_scale_policy.py --report bin/PathTracingValidation/completion-reproduction/part3/report.json --output bin/PathTracingValidation/completion-reproduction/part3/policy-assessment.json
python -B Tests/PathTracing/measure_performance.py --output bin/PathTracingValidation/completion-performance-repeat --cases baseline --warmup 64 --frames 64 --repeats 3
```

Use a new output directory and a freshly built Debug application. NumPy is required for native-buffer
analysis and quadrature. Keep generated PFM/PNG/PTBUF/logs out of commits. The aggregate runner serializes
GPU processes, retains individual failures, and performs numeric supported-policy validation for Part 3.
