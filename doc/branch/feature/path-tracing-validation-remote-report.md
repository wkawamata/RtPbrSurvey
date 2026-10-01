# Path Tracing validation report: Parts 1 and 2

Date: 2026-10-01. Workspace: `C:\work\RtPbrSurvey`.
Branch: `codex/path-tracing-validation-part1`, based on remote main `816c3f7` (PRs #82/#83/#84 included).
The Debug application was built at that commit; new scene/preset assets were measured from the working tree.
Their exact hashes and commands are recorded in `path-tracing-validation-results/part-1-summary.json`.

## Delivered

Four editable SceneDocument JSON fixtures and relative render presets are under
`Assets/Scenes/PathTracingValidation/`: constant environment, single-light visibility,
two-surface indirect reflection, and metallic roughness comparison.
No shader, renderer, engine, camera API or capture API changes were made. A subsequent App camera initialization fix is described below.
Scene Editor uses the same schema; user load/edit/save instructions and scope are in
`Tests/PathTracing/PART1.md`, linked from the existing README.
All four fixtures were loaded by the application and captured. Interactive editor save/reload was not automated.

`validate_part1.py` reuses the existing HDR capture/PFM/ROI reader and preserves source assets.
It writes derived variants and records failures, settings, commands and hashes in ignored output.
`test_validate_part1.py` adds four tests of attenuation and fixture references/radiance mode.

## Validation

- Debug x64 MSBuild succeeded; existing toolchain macro redefinition and vcpkg import warnings occurred.
- GPU: NVIDIA GeForce RTX 3080 Laptop GPU, driver 616.64 (Windows 32.0.16.1664).
- Ten Python tests passed: four new contracts and six existing HDR reader/statistics tests.
- Thirteen PFM captures and four PNG captures: 1920x1080, four samples, seed 7 (one seed-8 variant), ROI (952,532,16,16).
- No D3D12 error/corruption, process failure, timeout or non-finite HDR occurred in the corrected run.
- Each capture emitted two or three existing buffer InitialState warnings; they were retained in logs.
- Constant-MIS fixed-seed PFM hashes matched; seed 8 differed. Modes 1/2/5 were exercised.

| Check | Observation | Decision |
| --- | --- | --- |
| Visibility | Clear mean 0.08193004; between 0; beyond 0.08193004; maximum beyond difference 0 | Passed predeclared thresholds |
| Point attenuation | Expected ratio 4.01202826; measured 4.00515281; relative error 0.17137% | Passed 2% tolerance |
| Indirect contribution | One-bounce RGB (0.08108261,0.08108261,0.08108261); four-bounce (0.09651842,0.08283577,0.08283577) | Observed red contribution; no convergence claim |

The attenuation ratio is an independently evaluated analytic formula with a small-ROI spatial approximation.
It does not validate absolute GGX radiance. Metallic=0 retains a specular term; pure Lambert cannot be represented
by the current SceneMaterial. Constant-environment fixtures therefore start with sampling comparison, as requested.
Four-sample mode differences and the roughness image are smoke observations, not evidence of a preferred sampler.
Part 2 must use independent finite-sample references and multiple sphere ROIs/sample counts/seeds.

## Retained unsuccessful first run

The first draft preset incorrectly selected Albedo output (0) instead of Radiance (3), and the initial wall
orientation faced away from the camera. Its results remain at `bin/PathTracingValidation/part1/report.json`:
fixed-seed repeat passed, but seed variation, visibility and falloff failed. This was a fixture configuration
mistake, not an estimator defect. The presets and wall orientation were corrected without relaxing thresholds.
The fixture test and runner now require Radiance mode. Corrected captures remain separately at
`bin/PathTracingValidation/part1-radiance/`. Report hashes and representative artifact hashes are in the summary.

## Reproduction and next work

```powershell
python -B Tests/PathTracing/test_validate_part1.py
python -B Tests/PathTracing/test_compare_hdr.py
python -B Tests/PathTracing/validate_part1.py --samples 4 --output bin/PathTracingValidation/part1-reproduction
```

On this PC, Python is at
`C:\Users\wkawa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`.
Use the Debug build command from AGENTS.md before captures. Artifacts are local ignored files, not shared uploads.
Implementation and result metadata are committed locally. Push and the combined Part 1+2 PR have not been performed.
Parts 3-5 remain separate follow-up tasks.

## Interactive camera follow-up

The user inspected all four scenes interactively. Foreground FreeLook updates revealed an inverted
initial pitch in CLI SceneFile loading and Scene Editor preview rebuilding: the stored look-at target
was correct, but the Euler pitch had the opposite sign for DirectX rotation. Automated hidden captures
did not exercise the foreground keyboard update, so the original HDR results remain valid for their
recorded saved camera. The two App initialization sites now use negative asin(direction.y).
The user confirmed the second scene was corrected and viewed all four scenes with the CameraFix build.
The normal Debug build was subsequently updated. The DebugCameraController regression test confirms
that a no-input FreeLook update preserves the downward view from (0,4,-7) toward the floor.
No SceneDocument schema or camera API was changed.

## Part 2: static convergence (2026-10-02)

Local implementation commits: `d0ea693` (Part 1), `302a057` (camera initialization),
`12fa71f` (Part 2 runner/statistics/tests). Result metadata is saved in the following report commit.

The Debug binary at commit `302a057` was reused; estimator/shader/engine code was unchanged.
The measurement runner and statistics extensions were measured from the working tree. Exact commands,
scene/preset hashes, GPU/driver/build, ROI/sample/seed settings, metrics and artifact paths/hashes are in
`path-tracing-validation-results/part-2-summary.json`. This commit identifies the tested renderer,
not a claim that the new Python runner had already been committed at capture time.

Two initial 8 spp smoke captures took about 13 seconds each. Before reviewing convergence results,
the protocol selected 8/32/128 spp, evaluation seeds 1/2/3 and a common 512 spp reference averaged
from independent seeds 101/102. Constant-environment floor and three metallic sphere ROIs compare
BSDF (1), uniform NEE (2) and MIS (5) with two bounces. The direct/indirect controls use the same red-wall
fixture with separate one/four-bounce cohorts and local lighting, environment disabled, mode 0.
Each fixed-bounce cohort has its own common reference. Resolution is 1920x1080; no camera motion,
automatic exposure, emissive term or Russian roulette. See `Tests/PathTracing/PART2.md` for the full protocol.

The 60 environment captures and 24 lighting-control captures completed, plus four smoke captures.
Four complete-PFM fixed-seed repeats matched. Scene/preset hashes were consistent within every cohort.
PFM hashes were checked again when producing the summary. No non-finite HDR, process failure,
timeout, capture failure or D3D12 error/corruption occurred; existing buffer InitialState warnings remain in logs.
Thirteen Python tests passed, including ROI orientation, already-normalized HDR values, unbiased seed
variance, RGB means and reference standard error. No generated image, package or raw log is committed.

Representative mean-image RGB RMSE against the common finite-sample reference:

| ROI / estimator | 8 spp | 32 spp | 128 spp |
| --- | ---: | ---: | ---: |
| Floor / BSDF | 0.020443 | 0.010896 | 0.007004 |
| Floor / NEE | 0.140360 | 0.070652 | 0.035242 |
| Floor / MIS | 0.033056 | 0.016868 | 0.009524 |
| Roughness 0.18 / BSDF | 0.029457 | 0.016362 | 0.010309 |
| Roughness 0.18 / NEE | 3.113213 | 1.664028 | 0.786115 |
| Roughness 0.18 / MIS | 0.049728 | 0.025107 | 0.013938 |
| Direct-only floor / 1 bounce | 0.00003506 | 0.00001828 | 0.00001003 |
| Indirect floor / 4 bounces | 0.004286 | 0.002241 | 0.001284 |

All 14 measured series reduced both mean-image RMSE and seed variance at the three sample counts.
At equal spp, uniform NEE had particularly high variance on the smoother metal in this constant environment.
This is an observation about these fixtures and one GPU, not a general sampler recommendation.
Equal spp does not imply equal work; MIS samples both techniques, and GPU cost comparison belongs to Part 4.

Reference-seed disagreement RMSE was 0.010060 for the floor, 0.015164/0.015061/0.015659 for
roughness 0.18/0.4/0.8, 0.00001048 for direct-only, and 0.001346 for the four-bounce cohort.
Several 128 spp mean-image errors are comparable to these disagreements. More reference samples/seeds
are needed before estimating residual bias; the disagreements are not confidence bounds.
The reference shares the renderer implementation, so even agreement cannot establish absolute correctness.

At 128 spp, one-bounce mean RGB was (0.080994,0.080994,0.080994); four-bounce mean RGB was
(0.096246,0.082810,0.082810). This records red indirect contribution. It is a comparison of path contributions,
not a claim that increasing the bounce limit improved convergence.

Main capture elapsed times totaled about 14.5 minutes; controls about 8.2 minutes. These include process
startup, capture and readback and must not be interpreted as isolated PathTracingPass GPU benchmarks.
Complete JSON/MD and standalone PNG/SVG plots remain at `bin/PathTracingValidation/part2-main/`
and `bin/PathTracingValidation/part2-controls/`. Reproduction commands are in PART2.md and the summary.
Plotting requires matplotlib; on this PC its isolated installation is under
`bin/PathTracingValidation/python-packages/`. Statistics and capture retain standard-library-only dependencies.

The moving-camera fixed noise observation and proposed RNG/history separation are recorded in
`path-tracing-validation-moving-camera-note.md`. No estimator change was included.
Next options are a higher-sample reference for bias questions, a combined Part 1+2 PR, or separately
starting Part 3 after the user chooses the next task.

Status: done
