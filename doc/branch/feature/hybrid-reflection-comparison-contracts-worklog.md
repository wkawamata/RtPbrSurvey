# Hybrid Reflection Comparison Contracts Worklog

## 2026-08-31

- Started `features/hybrid-reflection-comparison-contracts` from current `main`, including PR #43 squash commit `18b642b`.
- Audited the existing reflection-resource contract, denoiser contract, production quality gates, DLSS SR/RR investigation, debug views, and LightPass/ToneMap boundaries.
- Separated comparison boundaries into current reflection, resolved reflection, spatial reflection, Final Lit, and presentation output.
- Fixed a mode matrix that marks Raster/Hybrid as implemented and PT/RR as unimplemented.
- Fixed paired conditions for camera, scene, resolutions, exposure, timeline, sample sequence, history reset, and temporal/spatial/upscaler settings.
- Explicitly kept the Current-Estimator Mean Baseline distinct from physical GT and prohibited calling high-SPP PT ground truth without matched declared conditions.
- Advanced the HDR diagnostic report to schema v15 and added comparison signal boundaries, rendering path, output size, camera snapshot, tone-map/exposure state, stochastic state, and hit-normal source. Existing statistics and radiance semantics are unchanged.
- Verified schema v15 with a two-warm-up/two-measurement-frame Estimator Test smoke: exit code zero, 1920x1080 render/output, perspective camera, exposure `1.0`, and zero D3D12 errors.
- Debug x64 built with zero errors and only the known duplicate-vcpkg-task import warning.
- Added `Test-ComparisonMetadataReport.ps1` to validate required schema v15 comparison fields and record the source revision in a harness-owned validation manifest instead of embedding source-control discovery in the application.
- The validator passed the schema v15 smoke report and rejected an intentionally corrupted `evaluatedRadiance` boundary with exit code 1. The generated manifest records source revision, scene, domain, render/output sizes, and exposure.

Status: in progress
