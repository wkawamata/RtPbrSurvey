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

## Current Execution State

The first attempt is incomplete, not qualified evidence:
`bin/PathTracingValidation/nrd-component-refined-20261020/report.json`.
Status: `failed-execution`. Both historical Total captures were revalidated,
but zero new 32768-sample component captures completed. No 42-row refined
qualification or NRD output comparison was produced.

The seed-11 Diffuse process returned exit code 0 without its capture file or
a capture-completion log. Its log contains startup information and the known
buffer initial-state warnings, but no D3D12 error. The process was responsive
when polled during acquisition. The reason for the early exit is not known;
manual window closure has been asked about but is not assumed. This failure
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

NRD Python tests: 184/184, including the six new refinement tests. No renderer,
shader or preset was changed. Long acquisition should be retried in a new
evidence folder after clarifying the unexpected exit; the existing failed
attempt and smoke files must not be overwritten or combined into a successful
refinement cohort. The eight previously unqualified pairs remain unqualified.
