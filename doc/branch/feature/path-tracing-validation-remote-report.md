# Path Tracing validation report: Part 1

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
Implementation and result metadata are committed locally; no push or PR is part of this Part-1-only request.
Part 2 (convergence), followed by the combined Part 1+2 PR, remains separate work.

## Interactive camera follow-up

The user inspected all four scenes interactively. Foreground FreeLook updates revealed an inverted
initial pitch in CLI SceneFile loading and Scene Editor preview rebuilding: the stored look-at target
was correct, but the Euler pitch had the opposite sign for DirectX rotation. Automated hidden captures
did not exercise the foreground keyboard update, so the original HDR results remain valid for their
recorded saved camera. The two App initialization sites now use negative asin(direction.y).
The user confirmed the second scene was corrected and viewed all four scenes with the CameraFix build.
The normal Debug build was subsequently updated. The DebugCameraController regression test confirms
that a no-input FreeLook update preserves the downward view from (0,4,-7) toward the floor.
No SceneDocument schema or camera API was changed. Convergence measurements remain Part 2 work.

Status: done
