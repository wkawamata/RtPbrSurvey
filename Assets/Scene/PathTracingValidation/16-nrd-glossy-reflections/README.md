# Static glossy reflection quality

Four opaque spheres use dielectric/metal crossed with roughness 0.6/0.1. Panel
positions match scene 15. A bright procedural softbox over a dark background
creates curved, high-contrast reflected features; no external assets are needed.
The preset must use Environment Map / Importance MIS (mode 7) and Skybox ON.
Constant White / MIS (mode 5) deliberately ignores environment-map illumination;
Skybox OFF uses the fixed clear color for primary misses. These modes are useful
for other tests but invalidate this fixture's requested illumination/control.

```powershell
python Tests/PathTracing/validate_nrd_glossy_quality.py --output bin/PathTracingValidation/nrd-glossy-new
```

Use a new/empty output directory and the NRD diagnostic Debug build. The runner
captures five deterministic resource runs after 30 warm-up frames, then two
independent 1024-spp Native references (seeds 11/29). Before references, a sixth
capture disables NRD and zeros every procedural environment color/intensity.
With direct lights and emission disabled, its RGB must be <= 1e-6 everywhere.
Otherwise the runner stops as inconclusive-environment: intended lighting has
not been qualified, and no NRD quality pass/failure is issued. The Native candidate has
accumulated 30 samples; NRD has 30 frames of single-sample history. This is not an
equal-time or equal-total-ray performance comparison.

Material masks use captured albedo with document sRGB8 quantization accounted
for. Three pixels are eroded at silhouettes; all remaining pixels are evaluated,
including any Native fallback. At least 90% must actually use admitted NRD, with
at least 100 interior pixels per material. Background must remain exact Native.

Criteria fixed before measurement:

- Pair-reference NRMSE at most 3%, normalized by reference RGB RMS.
- Reference luminance p95/p5 contrast at least 2.
- NRD RMSE no greater than the accumulated Native candidate RMSE.
- NRD mean RGB-vector bias at most 5% of the reference mean-vector magnitude.

Unconverged references and inadequate contrast are explicitly inconclusive,
not NRD quality passes. A qualified but regressing result is reported as a quality
failure without relaxing limits. Reports retain commands, hashes and metadata.

This measures static image error, not every-frame stability, moving objects,
camera cuts, perfect-mirror transport or human acceptance. It cannot establish
general specular quality or change the experimental/default-off NRD policy.
