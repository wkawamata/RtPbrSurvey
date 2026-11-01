# Volume and PT RR Main Integration

Date: 2026-10-28.
Workspace: `C:/work/RtPbrSurvey-work-3`.
Feature branch: `codex/pt-volume-transmission`.
Upstream main: `d45d0021582197a2c6cc4972933d7cbcb5529331`.

## Upstream Changes

- PR #105: shared PT reconstruction inputs and experimental live RR.
- PR #106: viewport-sized scene buffers and full-window UI.
- PR #107: general opaque PT RR inputs, coherent multi-SPP and jitter.
- Existing thin/rough/MASK work from the feature branch is retained; PR #104
  is not independently merged or closed by this integration.

## Resolution Policy

Use the new reconstruction backend selection and history model while retaining
the feature branch's Native fallback for thin transmission, alpha MASK and volume.
The shared engine surface gate suppresses NRD/RR conversion, RR SDK evaluation,
RR-specific jitter and retained RR camera history for unsupported surfaces.
Requested backend and actual output remain distinct in diagnostics.
This does not qualify RR/NRD for glass or enable experimental RR by default.

The shader test executable retains both sets of validation modes. Normal vcxproj
builds mark six new SDK-neutral helper sources as PCH-independent; the resource
helper also gets per-file NOMINMAX to match its CMake environment. Initial normal
build failures (C1010 and max macro expansion) are fixed by these project settings.

## Validation

- CMake Debug with NRD and RR diagnostics enabled: build succeeded, 0 errors
  (7 linker warnings). Build record:
  `C:/work/RtPbrSurvey-agents/volume-main-rr-merge-build-20261028`.
- Normal vcxproj Debug x64: succeeded, 0 errors, existing vcpkg MSB4011 warning.
  Final record: `C:/work/RtPbrSurvey-agents/volume-main-rr-merge-msbuild-r3-20261028`.
- CTest: 53/53 passed, including both RR and Volume contracts.
- Volume Python tests: 19/19 passed.
- Boundary/TIR GPU: 12/12, `bin/PathTracingValidation/volume-main-rr-merge-boundary-20261028/report.json`.
- Absorption GPU: 18/18, `bin/PathTracingValidation/volume-main-rr-merge-absorption-20261028/report.json`.
- Actual live RR on opaque inputs: 10/10, including multiple SPP, jitter, native
  output, and rejection controls. Report:
  `bin/PathTracingValidation/volume-main-rr-live-r2-20261028/report.json`.
  First launch stopped before GPU work because the inherited RR fixture loader
  omitted UTF-8 on scene JSON; explicit UTF-8 reads fix Japanese descriptions.
- The glTF runner preserves its default six cases. `--rr-bypass` explicitly adds
  a seventh Radiance case requiring Native output, no RR conversion and zero SDK
  evaluations even when RR is requested. All 7 passed, with missed exits 0 and
  unchanged absorption/geometry budgets. Final report:
  `bin/PathTracingValidation/volume-main-rr-merge-gltf-final-20261028/report.json`.
  The RR-requested Volume case reports incompatible-inputs, waiting-production,
  no conversion and zero SDK evaluations. Opaque RR positive cases above reach
  verified presentation, so this is not a backend-unavailable-only test.

GPU reports use the RR-enabled CMake executable. Normal vcxproj keeps main's
default build policy; live PT RR remains a CMake opt-in via
`-DRTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS=ON` plus runtime opt-in.
No full long-duration RR lifecycle/quality or NRD quality suite was rerun here.

## User Changes

The uncommitted scene10 file is excluded from all commits and retained unchanged,
verified by a local SHA256 comparison before and after integration.
Local editor state, screenshots, logs and build outputs are not committed.
