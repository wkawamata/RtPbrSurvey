# NRD Stable Radiance

Fixed-camera, opaque-material input/composition contract fixture:

- Left: lit dielectric with emission (0.8, 0.3, 0.1).
- Center: KHR_materials_unlit with base color (0.2, 0.4, 0.6).
  Deliberate emissive factors (0.9, 0.7, 0.5) must be ignored.
- Right: ordinary lit dielectric; no stable radiance.
- Gaps: primary misses with nonzero background radiance.

Load scene.json in Scene Editor. The preset requests diagnostic NRD RELAX,
one sample/frame, pixel-center primary rays and dedicated distance diagnostics.
An SDK-diagnostics-OFF build correctly remains Native; it cannot qualify this
live integration test. NRD remains experimental, not generally available.

Unlit keeps a valid primary hit position but no dedicated PBR distance samples.
It must be excluded by conversion/admission, then use exact decoded Native color.
Lit emissive pixels must actually enter NRD composition; checking only a Native
fallback would not verify that stable emission is added exactly once.

```powershell
python Tests/PathTracing/validate_nrd_stable_radiance.py --output bin/PathTracingValidation/<new-empty-directory>
```

The runner captures nineteen raw resources at the same deterministic frame,
requires matching metadata and successful recorded/presented NRD, checks material
coverage, and applies the existing independent CPU composition expression.
Stable-radiance RGB tolerance is absolute 1e-6; composition retains its existing
8-ULP(float32) plus 1e-6 bound. Unlit/background fallback uses exact decoded Native
values. Captures, logs and report.json are generated evidence, not source assets.

This does not qualify animation, temporal stability, general denoising quality,
transparent materials or final ToneMap appearance. Existing visual checks remain
separate.
