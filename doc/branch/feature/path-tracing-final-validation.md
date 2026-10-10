# Path Tracer correctness completion: Step 9

Date: 2026-10-10. Workspace: C:/work/RtPbrSurvey-work-3.
Branch: codex/pt-correctness-completion. Tested HEAD: 131f328.

## Purpose And Scope

Close the current correctness investigation with a bounded regression and an
evidence index. This is not a claim of complete glTF compliance, an unbiased
infinite-bounce solution, production performance or completed denoiser integration.
Long-running convergence, scale and performance campaigns remain historical
evidence; they are not silently presented as freshly repeated Step 9 tests.

The new run_final_regression.py uses one explicit Debug executable for every GPU
child, serializes GPU tasks, rejects missing/incomplete reports and validates the
child executable hash. Child exit code zero alone is insufficient. CPU test output
must contain a nonempty completed suite. Raw buffers, logs and runtime reports are
kept under ignored bin/PathTracingValidation, never committed.

## Current Regression

- [x] Debug x64 ALL_BUILD: successful, zero warnings/errors.
- [x] Python: 148/148 unit tests passed.
- [x] CTest: 26/26 tests passed.
- [x] Primary NormalRoughness, shifted ViewZ, marker Albedo and moving-camera MV.
- [x] Fixed-camera static/positive-X/negative-X object MotionVectors.
- [x] Nineteen GPU accumulation lifecycle/change checkpoints.
- [x] Fifteen emissive visibility and estimator-fallback captures.

The primary NormalRoughness max error is 0.00048828125; shifted ViewZ max error
is 0.000004768371582; marker Albedo max error is 0.00044505178 with zero material
classification mismatches. Moving-camera MV max bound-normalized error is
0.5387091, below 1, using the established 2 half ULP + 3e-6 NDC criterion. Its
legacy absolute 2e-5 criterion still fails (max error 0.00003070534). The regression
preserves that distinction and does not introduce a new relaxed tolerance.

Raw campaign: bin/PathTracingValidation/completion-step9-final-20261010.
Build logs: C:/work/RtPbrSurvey-agents/pt-final-regression-20261010.
No PT C++/HLSL changes were made in Step 9; the runner, tests and this report were
uncommitted when the campaign was launched. Actual executable/source/log hashes
are recorded in its report. CPU and GPU phases are reported independently.

All six phases passed, totaling 41 fresh GPU captures (4 primary guides, 3 object
motion, 19 history checkpoints, 15 emissive controls). The executable SHA256 is
65754f1fcd88aea1ae01a6964c355b17b3c866b91d201f5346983d4b0e8862da,
unchanged from the Step 8 guide campaigns. GPU: RTX 2080 Ti, driver 616.56.
All captured GPU log checks pass with no ERROR/CORRUPTION entries. Known buffer
initial-state warnings remain distinguishable from errors in the raw child logs.

Object MV max error remains 0.000002301931 NDC. All history pause/reset/fresh and
changed-state comparisons are exact; four-sample batching max absolute error is
0.000000178814 with a 0.000002 limit, relative RMSE 0.000000444929 within 1e-6.
Emissive baseline/backface/beyond/empty/shadow-off controls all pass for BSDF,
NEE and MIS at 64 samples, seed 11, a 64x64 receiver ROI. These equality/visibility
controls are not new convergence evidence.

Decision: the bounded current Native PT correctness snapshot has no blocking
regression in the tested scope. Steps 1-8 evidence and their exclusions remain
part of that conclusion. The next delivery action is to commit the Step 9 tools
and report, then review/merge the branch when requested. Further renderer features
or cross-GPU certification should be separate work, not implied by this closeout.

```powershell
python -B Tests/PathTracing/run_final_regression.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/final-regression
```

Requires Windows, a built Debug application/C++ tests, Python with NumPy and
CTest on PATH. Use an empty output directory. This is a functional/numeric
regression, not a GPU timing benchmark. It does not change driver power settings.

## Evidence Index

| Step | Evidence | Important qualification |
| --- | --- | --- |
| 1 | path-tracing-correctness-completion.md; completion-steps-1-3-summary.json | Post-fix regression, finite sample counts, one measured GPU |
| 2 | Same document: indirect transport/GGX/RR | Fixed-bounce comparisons and numeric quadrature, not infinite-bounce proof |
| 3 | Same document: self-intersection policy | Relative world-unit settings validated; fixed bias is not scale-independent |
| 4 | path-tracing-material-geometry-validation.md; completion-step-4-*-summary.json | Supported opaque packed-float glTF subset; PT/GBuffer transform and factor tests |
| 5 | path-tracing-emissive-mis.md; completion-step-5-*-summary.json | Emissive NEE/MIS, visibility, RR and finite-reference convergence; statistical limits retained |
| 6 | path-tracing-history-validation.md; completion-step-6-*-summary.json | GPU lifecycle and observable camera/light/material/geometry/resize mutations |
| 7 | path-tracing-performance-validation.md | Accepted Debug fixture measurements required controlled clock/foreground conditions; not rerun here |
| 8 | path-tracing-guide-contract.md; completion-step-8-*-summary.json | Native guide semantics, motion, spatial textures and mixed hit/miss; not backend-ready encoding |
| 9 | This document and completion-step-9-final-summary.json | Bounded current regression, not a rerun of every historical campaign |

Summary JSON files live under doc/branch/feature/path-tracing-validation-results.
Their own commit/binary/source provenance, rejected pilots and scope qualifications
remain authoritative; the index does not turn every historical result into evidence
for the newest renderer binary.

## Remaining Boundaries

- Cross-GPU/Release validation remains separate from the measured RTX 2080 Ti Debug scope.
- Negative SceneDocument scale is invalid input; glTF-baked reflection is a different supported path.
- Sparse/interleaved/non-triangle inputs, alpha MASK/BLEND, transmission, true
  double-sided semantics, alternate UV sets, skinning and morphing are not certified.
- Boundary reference classifications can differ at float32 precision. Step 8 keeps
  strict failures and qualifies its background-interval policy rather than removing pixels.
- Native diffuse/specular signals are not demodulated, and primary hitT is not an
  NRD normalized/secondary hit-distance contract. Mixed pixels need explicit
  background/coverage handling, or an independently validated 1-sample/frame adapter.
- Denoising, ReSTIR, radiance caching and broad performance optimization are later
  features; they should retain the current Native path as an inspectable reference.
