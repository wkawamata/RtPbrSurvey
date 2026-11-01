# Hybrid Diffuse IBL visibility diagnosis

Date: 2026-10-31. Source baseline: PR #109 / `12912e2`.

## Question

Does Hybrid diffuse environment lighting respond to scene occluders, separately
from missing indirect bounce lighting? This investigation adds fixtures and a
capture runner; renderer shading is unchanged.

`shaders_LightPass.hlsl` samples diffuse irradiance using the surface normal.
`PbrLighting.hlsli::EvaluatePbrDiffuseIbl` multiplies it by albedo,
`(1-metallic)`, material occlusion and `1/pi`. This path has no position-dependent
environment visibility query. The direct-light shadow mask does not multiply
diffuse IBL. Material AO is therefore not a substitute for scene visibility.

## Controlled Conditions

Run from the repository root with Python, numpy and Pillow:

```powershell
python Tests/PathTracing/inspect_hybrid_ibl_visibility.py --output bin/PathTracingValidation/hybrid-ibl-visibility-new --samples 128
```

The runner reuses the PR #109 Cornell/Sponza fixture construction. It generates
editable scene/preset files beneath a fresh output directory and leaves original
assets unchanged. Every case uses 640x360, exposure 1, a uniform gray procedural
environment (sky and ground 0.5), no direct lights, no emission, no skybox,
no temporal upscaling, no RR/NRD, and no Hybrid reflection contribution.

Cornell uses the five inward-facing walls without spheres. The camera is inside
the box at `(0,0,-1.2)`, looking at its back wall. A separate opaque cube closure
behind the camera seals the front opening without changing the visible surface.
Both open/closed variants capture Hybrid IBL OFF, Hybrid diffuse-only IBL, and
Native PT at two segments. This renderer counts the primary hit and the
environment miss separately: `maxBounces=1` suppresses environment NEE and
continuation entirely, and cannot serve as a positive environment control.
Two segments test environment visibility without wall-to-wall color transport.
A closed nonemissive box must receive no external
environment light; the open counterpart provides a positive control.

Sponza captures Hybrid OFF/diffuse-only/specular-only/both, plus Native PT at
two/eight segments. Geometry, camera, environment, tone mapping and exposure are
fixed. The component toggles identify which IBL term supplies light; they do not
prove the original directional-environment brightness difference has one cause.

The fixed central ROI is `(220,120,200,120)`. In Cornell it is the back wall.
Sponza ROI statistics describe an image region, not a semantic material mask.

## Evidence And Limits

The report records commands, input/executable/shader/source hashes, PT runtime
samples/backend/bounces, ROI display-space statistics and Debug Layer warnings.
Capture failures and D3D12 errors fail the run. Controls are recorded individually
instead of treating image equality between Hybrid and PT as a quality target.

PNG metrics are tone-mapped display values, not linear radiance. Exact equality
under the Cornell closure and zero-light controls isolate missing visibility;
they do not quantify an HDR energy error. The 128-sample PT results are an
inspection budget, not a convergence certificate. Specular IBL remains a
separate effect. Ambient Fill, AO tuning and DDGI are not implemented here.

Generated images, scenes and logs stay under ignored `bin/`. Contact sheets are
`cornell-contact.png` and `sponza-contact.png` beside `report.json`.

## Results (2026-10-31)

GPU: NVIDIA GeForce RTX 2080 Ti. Debug x64 build succeeded (0 errors, the
existing MSB4011 duplicate-import warning). Two fixture isolation tests passed.

Corrected Cornell evidence:
`bin/PathTracingValidation/hybrid-ibl-cornell-r2-20261031/report.json`.
All six captures completed, all five diagnostic controls were true, and no
D3D12 ERROR/CORRUPTION was captured. Existing resource initial-state warnings
remain (two per open capture, three per closed capture).

Central ROI mean, normalized PNG display luminance:

| Lighting | Open front | Closed front |
| --- | ---: | ---: |
| Hybrid IBL OFF | 0 | 0 |
| Hybrid diffuse-only IBL | 0.815686 | 0.815686 |
| Native PT, two segments, 128 spp | 0.384868 | 0 |

The Hybrid open/closed ROI arrays are exactly equal (maximum channel difference
0/255). The closed PT ROI is exactly black, while the open PT positive control
is nonblack. This establishes an environment-visibility limitation in the
controlled Hybrid diffuse IBL case, independently of missing wall-to-wall GI.
It does not mean Hybrid's open-front value should numerically match PT.

Sponza evidence:
`bin/PathTracingValidation/hybrid-ibl-visibility-20261031/report.json`.
The Hybrid central-ROI means were OFF 0, diffuse-only 0.271964, specular-only
0.136543, and both 0.364260. These values are after tone mapping and are not
additive. Native PT at eight segments/128 spp was 0.007072. The image shows much
less interior environment illumination in PT. This is consistent with missing
Hybrid environment visibility, but does not establish that every part of the
Sponza difference comes from that cause or that the PT estimate is converged.

The first run used `maxBounces=1` for the short PT controls and produced black
even with an open Cornell. Those PT controls are invalid as visibility evidence;
they were retained as pilot artifacts and replaced by the corrected Cornell
run above. The original Sponza Hybrid toggles and eight-segment capture remain
useful component observations. Its `pt-1` image must not be interpreted as an
occlusion result. The current runner uses two segments for short PT cases and
returns a nonzero exit if any Cornell diagnostic control fails.

Next step: design a separate environment-visibility term, retaining material AO
as a distinct input. A short-range AO-only approximation must be tested against
distant walls/roofs; a finite ray radius alone cannot guarantee enclosed-space
environment occlusion. Keep specular IBL, direct-light shadows, and any future
Diffuse GI addition independently controlled. This investigation does not add
Ambient Fill or tune exposure to hide the observed difference.
