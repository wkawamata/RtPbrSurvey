# Component Reference Refinement

## Protocol

This follows the 8192-sample qualification and qualified component comparison.
The purpose is to reduce reference uncertainty, not to change NRD filtering.
Runner: `Tests/PathTracing/refine_nrd_component_references.py`.

- Revalidate the frozen 8192-sample cohort and historical reference chain.
- Reuse the two existing 32768-sample Total references after full validation.
- Acquire four fresh captures: Diffuse/Specular, seeds 11/29, 32768 samples.
- Retain the same component-reference executable and PT shader as the reviewed
  8192-sample cohort. Do not rebuild or modify them during acquisition.
- Use one sample per frame via the CLI sample target, pixel-center primary
  sampling, four bounces and environment mode 7. NRD and distance diagnostics
  stay off. No camera, material, ROI or sampling-policy edits are allowed.
- Keep the existing 14 masks, minimum 100 pixels and pair-NRMSE limit 0.03.
- Require exact-zero pure-metal Diffuse references. Keep dark and noisy
  reference pairs inconclusive, not passed or blamed on NRD.
- Validate resource/format identity, runtime sample count, camera matrices,
  transport state, raw capture/log hashes, finite nonnegative RGBA and alpha 1.
- Save progress after each capture. A capture failure or timeout stops the
  study; the child process is terminated and the report records failure.
- The final runner logs FPS/frame checkpoints every 1024 frames so that an
  interrupted retry has more progress evidence than startup-only logs.
- On Windows, `--hidden` starts the existing executable with a hidden window
  to avoid accidental foreground camera input and manual resizing. No render
  settings change; metadata must still match the fixed 1920x1080 camera.
- Stop at this fixed budget even if a pair still does not qualify. A subsequent
  budget change requires a separate study; no post-hoc threshold relaxation.

Total and the fresh component references intentionally come from distinct
historically bridged builds. Their provenance remains separate. Reference
qualification is performed independently per component, not by presuming bit
identity across those builds or exact closure of separate FP16 captures.

## Interpretation

The final report must distinguish acquisition completion from qualification.
A `completed-component-reference-refinement` status only means the declared
study finished. `partially-inconclusive` remains a possible reference verdict.
The report includes both the new qualification and the previous 8192-sample
pair result for every component/ROI combination.

Even qualifying all pairs would not establish NRD input correctness, a filter
defect, a quality ceiling or default-on readiness. Component output comparison,
multiple output seeds, temporal regression and human review remain distinct.

## Earlier Execution Attempts

The first attempt is incomplete, not qualified evidence:
`bin/PathTracingValidation/nrd-component-refined-20261020/report.json`.
Status: `failed-execution`. Both historical Total captures were revalidated,
but zero new 32768-sample component captures completed. No 42-row refined
qualification or NRD output comparison was produced.

The seed-11 Diffuse process returned exit code 0 without its capture file or
a capture-completion log. Its log contains startup information and the known
buffer initial-state warnings, but no D3D12 error. The process was responsive
when polled during acquisition. The user subsequently confirmed manual window
closure as the cause. This failure
is not an NRD quality result. The current runner now explicitly rejects a
normal process exit without complete capture/log evidence, with a regression
test for that case. The first failed report retains the pre-guard runner hash
and is not presented as a successful run of the final source.

A subsequent 256-sample smoke test completed using the same fixture,
executable, component, seed and one-spp path. The sample target was 256 and
`-LogFPS 64` was added for diagnostics. The raw image is bit-identical to the
reviewed 256-sample Diffuse replay. Its completion log and runtime sample count
are present, and no D3D12 errors were captured.

Smoke report:
`bin/PathTracingValidation/nrd-component-refined-20261020/11/smoke-check.json`.
Status: `passed-short-smoke`.
SHA-256: `b66e1c0a7feea6bbf853f1e88a83ea1f061b823a81f311edcd5c38ac4079ed73`.
This checks short-run capture plumbing, not long-run stability or reference
convergence. A direct PowerShell GUI launch before the waited smoke attempt
produced no evidence and is excluded.

The initial runner passed 184 Python tests. No renderer, shader or preset was
changed. These failed/short attempts do not qualify any additional references.

### Visible Retry

The user authorized a retry in
`bin/PathTracingValidation/nrd-component-refined-20261021-retry`.
Its FPS frame counter passed 32768 without a completed capture. A read-only
window observation showed render/output size 1920x1009, not the required
1920x1080. The cause and time of that size change were not established.
FPS frames are not proof of accumulated samples when history can reset.
The agent stopped only the matching owned capture process; the runner recorded
`failed-execution`. No image from this visible retry is accepted reference data.
Debug-visibility observation was attempted only after detecting the mismatch;
no material, lighting or camera controls were changed for diagnosis.

### Hidden Launch Calibration

A hidden-startup 256-sample Diffuse capture at seed 11 completed at 1920x1080,
with exact bit identity to the reviewed Diffuse replay and no D3D12 errors.
Files: `nrd-component-refined-20261021-retry/11/hidden-smoke-256.ptbuf` and
`hidden-smoke-256.log` under `bin/PathTracingValidation`.
The process startup policy, not the render command's sampling inputs, differs.
The runner now offers explicit `--hidden`; normal startup remains its default.
The startup option is covered by a mock-based test. Python tests: 185/185.

## Completed Hidden Study

Report:
`bin/PathTracingValidation/nrd-component-refined-20261021-hidden/report.json`.
SHA-256: `12464ee792052aeba8616e528e68c17ec1f948f51526a124b88ef47bb508a8c8`.
Status: `completed-component-reference-refinement`.
Reference verdict: `partially-inconclusive`.

All four fresh component captures completed at 1920x1080 with exactly 32768
accumulated samples, one spp, the fixed camera and the unchanged executable/
shader. Both historical Total captures were revalidated and reused. Artifact
and fixture hashes remained unchanged throughout acquisition. Capture logs
contain zero D3D12 errors. No visible-retry or smoke data is combined into the
accepted reference cohort.

| Component | Qualified active pairs | Exact-zero inactive | Inconclusive |
| --- | ---: | ---: | ---: |
| Total (reused 32768-sample references) | 14 | 0 | 0 |
| Diffuse (fresh) | 6 | 7 | 1 |
| Specular (fresh) | 14 | 0 | 0 |

The new inventory is 34 qualified active pairs, seven inactive zero pairs and
one inconclusive pair, versus 27/7/8 in the 8192-sample study. The Total
improvement is reuse of the previously acquired high-sample evidence, not a
claim of two additional fresh Total runs.

The focus `dielectric-smooth/mirror-proxy` ROI retains the same 792 pixels:

| Component | Previous 8192 pair NRMSE | 32768 pair NRMSE | Qualification |
| --- | ---: | ---: | --- |
| Total | 4.7736% | 2.6533% | Qualified |
| Diffuse | 10.0104% | 5.0129% | Inconclusive |
| Specular | 4.2619% | 2.5636% | Qualified |

The roughly halved Diffuse pair difference after quadrupling the sample budget
is consistent with decreasing Monte Carlo reference variance, but does not
prove estimator unbiasedness or NRD correctness. The 3% criterion is unchanged.
No additional sampling is silently scheduled beyond the declared fixed budget.

## Remaining Work

- The newly qualified Total/Specular focus outputs and other recovered pairs
  have been compared in `nrd-refined-component-comparison.md`. The prior
  8192-based comparison remains separate and has not been relabeled.
- Keep the one Diffuse focus pair excluded from absolute component quality
  attribution until a separately declared reference study qualifies it.
- Consider validating a faster reference acquisition path before a substantially
  larger Diffuse sample budget. Do not reuse the rejected 16-spp acceleration
  or weaken numerical/variance checks merely to shorten execution.
- Preserve the default-off policy and pre-pass defaults. This run contains no
  new NRD output quality test, temporal regression or human acceptance test.
