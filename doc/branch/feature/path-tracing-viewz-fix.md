# Path Tracing ViewZ correction

Base: 01ea9af (merged PR #85). Branch: codex/path-tracing-viewz-fix.

Part 5 found that a front plane at view-axis depth 5 produced 3.637805 to 6.918375 with lens shift (.35,-.2). ComputePrimaryViewZ treated the inverse-projected far-center ray as camera forward. That ray tilts with lens shift.

The fix derives forward from the normal of the inverse-projected far plane: three far points identify its right/up directions; their cross product is the view axis, including for orthographic projection. The far plane avoids subtracting short near-plane edges from translated world coordinates. Camera constant-buffer layout, primary rays, motion, BRDF and material sampling remain unchanged.

Validation uses independent world ray/plane intersections and cameraTarget-cameraPosition depth, retaining the existing 1e-4 world-unit tolerance. The previous off-axis definition is reported separately. Known rolled/translated camera projection math is covered by a unit test. The new editable input-camera-transform fixture supplies position (3,2,-5), target (-1,.5,1), up (.2,1,.1), lens shift (.35,-.2).

Reproduction from repo root (NumPy available; --packages accepts local dependencies):

```powershell
python -B Tests/PathTracing/validate_inputs.py --base-commit 01ea9af --output bin/PathTracingValidation/viewz-fix-final --cases input-plane-ViewZ-static,input-shifted-ViewZ-static,input-ortho-ViewZ-static,input-marker-ViewZ-static,input-shifted-MotionVectors-moving,input-shifted-ViewZ-moving,input-camera-transform-ViewZ-static
```

Seven captures cover symmetric, shifted and orthographic depth; foreground first-hit distance; shifted moving depth; rolled/translated/up-tilted camera; and unchanged motion. Shader output remains a first-sample current-frame guide. All buffers are checked for finite values and D3D12 errors. Raw artifacts stay ignored.

Albedo gamma-2.2 decoding and shared scene material texture bindings are separate pending corrections. Historical Part 5 results remain unchanged.

## Measured results

Tested commit: 2a823559a6dab30929d3558d1f56376d6d95baa8, clean checkout. Debug x64 build passed; 31 Python unit tests passed. Seven GPU captures completed with finite buffers and zero D3D12 errors. [Machine-readable report](path-tracing-validation-results/viewz-fix-summary.json) includes commands, matrices, artifact hashes, source/binary hashes and GPU/driver details.

| Depth case | Maximum absolute error (world units) |
|---|---:|
| Symmetric perspective | 1.430511e-6 |
| Lens-shifted perspective | 4.768372e-6 |
| Orthographic | 9.536743e-7 |
| Foreground marker first hit | 4.768372e-6 |
| Lens shift with moving camera | 1.413911e-5 |
| Lens shift with translated, rotated and rolled camera | 4.684565e-5 |

All six depth cases pass the unchanged 1e-4 tolerance. The shifted front plane now reports 4.999995 to 5.000004 for the expected depth 5, replacing the historical 3.637805 to 6.918375 range. Moving MotionVectors remain within the pre-established half-float precision bound (maximum absolute NDC error 3.07919e-5); the older fixed 2e-5 threshold still fails as in Part 5. This correction does not claim improved motion precision.

Status: done
