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
