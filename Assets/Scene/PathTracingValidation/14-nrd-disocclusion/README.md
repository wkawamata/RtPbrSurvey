# NRD Disocclusion

Two opaque diffuse panels: a narrow red foreground at z=-1 and a wide green
background at z=1. A constant white environment illuminates both. The camera
starts at (0,0,-6). All geometry is static.

The stored description contains the Japanese visual checklist. Inspect the
foreground silhouette while orbiting: newly revealed green background must not
retain the old red silhouette. Ordinary GUI camera edits currently reset NRD;
continuous NRD history is available only through the diagnostic CLI flag.

```powershell
python Tests/PathTracing/validate_nrd_disocclusion.py --output bin/PathTracingValidation/<new-empty-directory>
```

The runner uses SDK diagnostics and final-image NRD preview. It captures Albedo,
MotionVectors, NrdSceneColor and Native SceneColor for static, +20 degree, -20
degree and +20 degree per-frame-reset cases. Motion spans the last twenty frames
of a thirty-frame warm-up. The reset control also restarts the SDK frame index;
it does not isolate temporal reuse from SDK sampling changes.

At a four-pixel sampling stride, independent finite-plane intersections identify
the current first hit. The world point is projected into the previous view, and
another ray/plane intersection checks whether the foreground previously hid it.
Current/previous plane edges and out-of-frustum points are excluded. Moving cases
require at least twenty sampled newly revealed points; all cases require at least
one hundred retained background and foreground points. Material Albedo, SDK
camera/reset state, resource/cohort identity and the existing half-format MV
numeric bound are checked separately.

Document colors are rounded to sRGB8 by SceneDocumentBuilder, then decoded on
the GPU. Albedo is therefore checked against that independent round-trip, not
the unquantized JSON value, with 2 ULP(decoded half) + 3e-6 allowance.

Color means and differences from same-frame noisy Native are measurements only.
The report always states qualityVerdict=not-evaluated, including when the
geometry/MV/history gate passes. A converged target and an explicit quality
criterion are required before judging ghosting or denoising improvements.

Not covered: object motion, camera cuts, motion reversal, settling, animation,
visual acceptance or general denoising quality. Captures/logs/report.json are
generated evidence and are not committed.
