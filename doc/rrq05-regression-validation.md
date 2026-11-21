# RRQ-05 chained RR regression

Status: completed on 2026-11-19. Chained remains opt-in.

The NRD+RR CMake Debug build passed all 22 ordered checkpoints, with exit code 0
and no D3D12 ERROR/CORRUPTION. The ordinary VS2022 Debug build also passed
(one existing MSB4011 warning). A generated single-instance opaque helmet scene
uses the tracked rr-quality scene's existing glTF asset and preset. Original saved
scenes/assets are unchanged; generated files use rebased relative asset paths.

## Coverage

- Continuous camera translation/rotation and object translation/rotation did not
  reset or cancel history and produced no input/output rejection.
- Four independent reprojection checks used captured current/previous camera and
  object matrices plus primary world positions, rather than the renderer's motion
  calculation. Maximum surface UV error was below 9.6e-7. Maximum sky UV error was
  below 5.7e-6. The tolerance is 3e-5 to account for raw RG16F motion quantization.
- All four stop phases returned dense motion below 3e-6 UV.
- Projection change, 1->4->1 spp, pixel-center->frame-jitter->pixel-center, and an
  actual SceneDocumentRuntimeScene::LoadFromFile plus GPU scene reload each reset
  history and produced exactly the next verified output with reset=true.
- Inf and FLT_MAX injected into one converted color pixel were sanitized before
  SDK consumption and rejected for publication. The retained output was byte-identical
  to the previous successful checkpoint. Each subsequent packet reset and recovered.
- Native and NRD selection performed no extra RR evaluates. The NRD checkpoint
  recorded ten SDK dispatches, selected the composed NRD output, and captured finite,
  nonuniform RGB. This is actual opaque NRD execution, beyond RRQ-04's selection test.
- Returning to RR produced a fresh verified output with reset=true.

Each checkpoint saved five buffers: motion, primary world position, RR color,
current PT radiance and selected backend output. This pins the moving packet during
numeric checks. All 22 selected outputs were finite and nonuniform. These checks
establish input/history consistency, not new subjective quality acceptance.

The test exposes actual NRD recorded/dispatch diagnostics and adds Inf/huge-value
faults to the existing process-local validator. They are not scene settings and are
never enabled during normal rendering. Existing default reconstruction policy is unchanged.

## Reproduction

Build the CMake application with NRD SDK diagnostics and RR conversion diagnostics
ON, then from repository root:

```powershell
python -B Tests/PathTracing/validate_rr_regression.py --exe build/reconstruction-input-validation/Debug/RtPbrSurvey.exe --output build/rrq05-regression-new
```

`--prepare-only` generates the fixture without using a GPU. Execution requires a
new/empty output folder and absence of other RtPbrSurvey processes. Other GPU tasks
must also be checked separately. Work5 was not terminated; GPU validation began only
after its process exited. No images were uploaded.

Local evidence: build/rrq05-regression-r3-20261119/report.json, d3d12.log and per-case
ptbuf files. Earlier failed preparations remain under rrq05-regression and
rrq05-regression-r2 (absolute asset-path contract and incorrect debug-view fixture
configuration respectively); these were test setup errors, not accepted runs.
The successful run's executable SHA256 is pinned in its report. Formatting-only
cleanup followed; ordinary Debug was rebuilt. NRD SDK sources were read from the
existing pinned dependency checkout, with all generated build output in Work4.

## Main integration follow-up

After integrating origin/main through 14c3d47, both SDK-enabled Debug and Release
builds passed the same 22-checkpoint regression, including four independent motion
checks and ten recorded NRD dispatches. Both processes exited 0. The Debug run had
no D3D12 ERROR/CORRUPTION. Evidence: build/rrq05-main-integrated-debug-20261119 and
build/rrq05-main-integrated-20261119, respectively. The integrated Release hash is
pinned in the RRQ-06 report.

[RRQ-06 performance/parity](rrq06-performance-and-parity.md) and
[RRQ-07 adoption decision](rrq07-adoption.md) are complete. Broader arbitrary scenes
and subjective moving-glass quality remain outside this automated regression claim.
