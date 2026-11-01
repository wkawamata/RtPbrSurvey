# PT Helmet Reflection Investigation

Date: 2026-10-28
Branch: `codex/pt-helmet-reflection-quality`
Base: `74481d4` (remote main, including Volume and Native PT RR integration)

## Scope

Improve practical Native PT material and reflection quality. Hybrid is an
observation/comparison path, not a target to tune or match. RR performance work
continues separately. Do not add ambient light simply to hide black lighting.

The user's lower-hemisphere observation is relevant: black environment radiance
can legitimately darken surfaces/reflections. Separate that from material import
and shading defects using a controlled environment with a nonblack lower half.

## First Findings (Before the Fix)

- DamagedHelmet uses one opaque metallic-roughness material, `Material_MR`.
  Its visor-like region is not an explicit transmission/glass material.
- It has a normal texture but its primitive contains POSITION, NORMAL and
  TEXCOORD_0 only; no TANGENT attribute.
- `LoadCommittedHitShadingNormal` in `Shaders/SceneRayQuery.hlsli` returns the base
  normal when the interpolated tangent is zero. This currently discards the
  normal texture for the PT helmet. The importer reports `MissingTangents`.
- The raster GBuffer has a fallback frame; making PT match that arbitrary frame
  is not the proposed fix. glTF specifies MikkTSpace tangent generation for this
  missing-attribute case. Correct UV-based tangent generation is the next defect
  to address, preserving supplied tangents, UV seams and handedness.

Reference: [Khronos glTF specification, meshes](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#meshes).

## Reproducible Scene

`Assets/Scene/PathTracingValidation/32-helmet-reflection/scene.json`

- Existing DamagedHelmet asset, no copied textures or material overrides.
- Procedural studio environment, groundColor=(0.25,0.25,0.25).
- No direct lights, no emission, Native PT, no NRD/RR.
- Default 4 bounces, pixel-center primary, deterministic seed 7.
- Scene includes Japanese observation instructions and is available in Saved Scenes.

`Tests/PathTracing/inspect_helmet_reflections.py` generates five diagnostic variants
without editing the saved scene or original asset. It records executable/source/
asset/capture hashes and checks runtime configuration and D3D12 errors. This is
an inspection report, not a quality acceptance or convergence certification.

## GPU Evidence

Report: `bin/PathTracingValidation/helmet-reflection-r2-20261028/report.json`

Existing Debug x64 executable, RTX 2080 Ti, 640x360, 64 samples, seed 7,
pixel-center primary, environment importance MIS, Russian roulette off.
All five captures completed; no D3D12 ERROR/CORRUPTION was recorded. No renderer
source changed in this investigation, so no new renderer build was required.

Full-frame linear HDR comparisons against original material at 4 bounces:

| Variant | Maximum absolute RGB difference | Changed pixels |
| --- | ---: | ---: |
| Remove normal texture | 0 | 0 |
| Black lower hemisphere | 0.260581 | 151448 |
| 2 bounces | 1.472351 | 14771 |
| 8 bounces | 0.019043 | 1991 |

The normal-texture pair is exactly equal, consistent with the missing-tangent
code path. This proves a concrete missing material feature; it does not prove
that this alone explains all of the user's reflection impression.
The environment comparison includes the background; it is not a helmet-only ROI
measurement. Bounce comparisons at one seed and 64 samples do not establish
convergence or an adequate bounce count for all scenes.

The first two automation attempts failed before capture because the generated
glTF used absolute external URIs, which this loader joined to its base directory.
Relative URIs fixed the fixture. Failed evidence remains under the first two
ignored output directories and is not counted as a successful comparison.
Four new Python unit tests passed. Existing renderer suites were not rerun.

## Tangent Fix (2026-10-29)

The baseline investigation was committed as `80c10ff` before implementing the fix.

- Pinned upstream MikkTSpace at `3e895b49d05ea07e4c2133156cfa94369e19e409`.
- Generate missing tangent frames from source glTF positions/normals/UVs before
  baked node transforms, then apply the existing LH and mirror conversions.
- Carry optional per-index-corner frames as CPU metadata. Preserve authored
  tangents, original raster vertices/indices, mesh ranges and BLAS triangle order.
- Duplicate only supplemental shading corners in separate PT vertex/index upload
  buffers. PT alone binds these buffers, with explicit persistent RenderGraph
  reads. Other paths continue to bind original geometry. Shader layout and root
  signature are unchanged.
- Degenerate UV/geometric frames and collapsed tangent directions retain base
  normals with an explicit `DegeneratePtTangents` warning. Nonfinite inputs or
  failed generation are not silently converted into arbitrary tangent frames.
- DamagedHelmet has 33 unavailable corners (11 triangles) out of 46356 corners.
  Ten of those triangles have exactly degenerate UVs. The remaining valid 46323
  corners receive PT tangent frames. This is not a claim of normal-map coverage
  for the degenerate triangles.

Final evidence:

- Regular vcxproj Debug x64: success, zero errors, one existing MSB4011 warning.
  Build record: `C:/work/RtPbrSurvey-agents/pt-helmet-tangents-msbuild-r2-20261029`.
- CMake Debug: success, zero errors, seven existing LNK4199 test-link warnings.
  Build record: `C:/work/RtPbrSurvey-agents/pt-helmet-tangents-cmake-r3-20261029`.
- CTest: 53/53 passed, including analytic tangent direction, mirrored UV seams,
  node nonuniform scaling/mirroring, authored tangent preservation, degenerate
  frames, invalid input and interleaved glTF/procedural ranges.
- Python: geometry fixture tests 7/7 and helmet inspection tests 4/4 passed.
- GPU guide suite: 7/7 passed, including generated tangent and generated mirrored
  cases, with unchanged absolute tolerance 0.001. Generated-case maximum error
  is 0.000126953125. Both generated guide arrays exactly equal their authored-
  tangent controls. Report:
  `bin/PathTracingValidation/helmet-tangent-guides-final-20261029/report.json`.
- Helmet diagnostic suite: five captures completed, no D3D12 errors/corruption.
  Report: `bin/PathTracingValidation/helmet-tangents-final-20261029/report.json`.
  Normal texture A/B now differs at 38366 pixels, maximum RGB difference 1.673584.
  The no-normal-texture control is exactly unchanged from the 2026-10-28 baseline.
  This isolates the intended material change from general lighting changes.
- Saved-scene smoke capture: 960x540, 128 samples, Native, 4 bounces, no Debug
  Layer errors. `bin/PathTracingValidation/helmet-tangents-final-20261029/helmet-native.png`
  shows the expected framing and restored small surface details without gross
  image corruption. This developer inspection is not user subjective approval.
- The first full-model attempt exposed a collapsed frame and did not complete;
  it is retained as failed evidence, not counted as a successful image run.
- Full Volume image suites, RR live/lifecycle quality and NRD subjective suites
  were not rerun. Hybrid was not visually requalified or tuned. Human approval
  of the corrected helmet is still pending.
- Other UV sets, texture transforms and per-texture sampler behavior retain the
  existing importer restrictions/warnings. This work does not certify those
  unsupported glTF features.

## Work Units

1. [x] Generate missing UV-based tangents using MikkTSpace, with CPU tests for mirrored
   UVs, seams, invalid/degenerate geometry and existing tangent preservation.
   Keep Hybrid shading/IBL behavior unchanged; review any shared import impact
   explicitly before adopting an integration path.
2. [x] Re-run original/no-normal A/B and add independent normal-guide evidence.
   [ ] User visual review of the actual helmet remains separate.
3. [x] Verify metallic/roughness input values against independent texture samples.
4. [ ] Qualify near-mirror BSDF sampling/evaluation and the roughness floor using
   reflection targets. Separately examine environment content/resolution.
5. [ ] Compare Native accumulation with RR at matched camera/material/environment.
   Denoising must not conceal a Native material integration failure.

The local scene10 user edit and existing generated/untracked files are retained
and excluded from this work. Local main's pre-existing divergent planning commit
is untouched; this branch starts directly from origin/main.

## Metallic/Roughness Input Check (2026-10-29)

The tangent fix was committed as `ffe44ad` before this investigation. This work
adds tests/reporting only; no renderer or Hybrid shading code was changed.

The [glTF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
defines roughness in G, metallic in B, both linear, multiplied by their material
factors. R/A do not participate. An absent texture contributes G=B=1. These rules,
not Hybrid output, define the material-input reference.

`Tests/PathTracing/validate_metallic_roughness.py` creates four cases and captures
NormalRoughness, MaterialF0Metallic and ViewZ for each, at 640x360, Native PT,
one pixel-center sample, one bounce, with direct/environment/emission disabled:

1. 2x2 RGBA PNG with distinct channels and metallic/roughness factors 0.7/0.6.
2. The same G/B values with only R/A changed.
3. No metallic-roughness texture; the same factors must survive the fallback.
4. Original DamagedHelmet geometry, UVs, material and JPEG texture.

The CPU reference parses only dense single-node/single-primitive opaque fixtures;
it is not a general replacement glTF loader. It independently applies source
node transforms/LH conversion, finds front-facing primary triangle intersections,
interpolates original UVs, and performs mip-0 repeat/bilinear texture sampling.
ViewZ verifies correspondence with the actual GPU surface before M/R comparison.
PNG controls also check F0 RGB using the known constant base-color factor.
The original helmet base-color RGB/filtering path is not qualified by this suite.

Evidence: `bin/PathTracingValidation/helmet-material-inputs-final-20261029/report.json`.

| Case | Matched points | Metallic max error | Roughness max error | Material tolerance |
| --- | ---: | ---: | ---: | ---: |
| Textured PNG | 437 | 0.00162201 | 0.00111420 | 0.004 |
| R/A control | 437 | 0.00162201 | 0.00111420 | 0.004 |
| Factor only | 437 | 0.00029297 | 0.00039063 | 0.004 |
| DamagedHelmet | 273 | 0.00438651 | 0.00355154 | 0.01968628 |

The tolerances were fixed before the GPU run. PNG tolerance covers hardware
bilinear fractional precision and half-float output; JPEG adds 4/255 for the
independent Pillow versus application decoder. No tolerance was relaxed after
measurement. Maximum ViewZ difference is 0.000001160 (tolerance 0.002).
R/A control guide arrays are exactly equal to the original PNG controls.
All four cases / twelve captures passed, with no D3D12 errors or corruption.
Five Python tests passed, including wrong M/R/F0/depth and missing surface rejection.
No new C++ build or full renderer test rerun was required for this test-only step.

The helmet front-center ROI (280,145,80,50) has median metallic 0.99560547 and
median roughness 0.00746155. Its 10th/90th percentiles are metallic
0.98090820/0.99951172 and roughness 0/0.05138855. This is a mostly metallic,
near-smooth region, not evidence of a transmission material. The ROI includes
material variation/scratches; these statistics do not label the whole asset.

Conclusion: no M/R channel, factor, linear-data decode or coarse UV correspondence
defect was found in the qualified cases. This does not certify the BSDF, final
reflection convergence, arbitrary glTF samplers, other UV sets, RR or NRD quality.

Next concrete question: `PathTracingSampling.hlsli` consistently uses
`alpha=max(roughness*roughness,0.001)` in sampling/evaluation/PDF. Below roughness
about 0.03162, this changes the requested lobe. The same numerical floor also
exists in the denoiser sampling helper. It is not a mismatched-PDF finding and is
not yet established as the cause of the user's impression. Qualify its influence
with a high-contrast reflection target before changing the floor or adding a
near-delta path; do not trade numerical stability for a sharper screenshot.

## Near-Mirror Floor Diagnostic (2026-10-30)

`Tests/PathTracing/validate_near_mirror.py` adds a generated reflection target:
a flat metallic mirror at renderer Z=0 and a 0.04m-wide, unit-radiance emissive
stripe at Z=-6, behind the camera at Z=-5. The stripe is visible only through
reflection. The final fixture's 6x4m mirror covers all primary rays; the clear
color is black. There are no normal maps, textures, direct lights, IBL, RR or
NRD. Native PT uses two bounces, pixel-center primary rays, BSDF-only emissive
sampling, seed 7, 640x360 and 256 accumulated samples per case.

Cases: roughness 0, 0.00746155 (helmet ROI median), 0.03, 0.06 and 0.15.
The diagnostic requires exact full-array equality of the first three captures
under the current floor, nonempty reflection and distinct above-floor controls.
It rejects missing cases, nonfinite/negative/out-of-range radiance and runtime
or D3D12 failures. Runtime settings and captured resource metadata are verified;
input, source, executable, log and capture SHA256 hashes are retained. Outputs
are ignored artifacts, not assets to commit. It is a reproduction contract for
the current implementation, not a desired permanent material behavior.

The analytic reference uses the GGX NDF half-vector distribution at normal
incidence: P(tan(theta_h)<=t)=t^2/(alpha^2+t^2), with reflected angle 2*theta_h.
For the helmet median, alpha=r^2=0.0000556767, versus the current 0.001 floor.
The median reflected angular deviation is approximately 0.00638 degrees without
the floor and 0.11459 degrees with it (about 18x). This is an unconditioned
NDF-sampling reference, not an image-space blur ratio or a radiance-profile
prediction: Fresnel/Smith weighting, invalid reflection directions, pixel
footprint and finite target geometry must be considered separately.

The initial pilot is retained as failed evidence at
`bin/PathTracingValidation/near-mirror-floor-20261030/report.json`.
Its smaller mirror exposed primary silhouettes and blue background; zero versus
helmet-median differed at one edge pixel in G/B (maximum 0.00156212), while
helmet-median and 0.03 were exactly equal. The cause of that edge discrepancy
was not established. The final fixture removes this confound rather than
relaxing equality tolerances or presenting the pilot as passed.

Final evidence:
`bin/PathTracingValidation/near-mirror-floor-fullframe-20261030/report.json`.
All five expected captures completed at 256 spp, with native output and no
captured D3D12 errors/corruption. Current source/input hashes match the report.
The first three full HDR arrays are exactly equal, including alpha.

| Roughness | Profile peak | Profile row sum | RMS width (pixels) |
| --- | ---: | ---: | ---: |
| 0 | 0.647384 | 1.949660 | 1.758908 |
| 0.00746155 | 0.647384 | 1.949660 | 1.758908 |
| 0.03 | 0.647384 | 1.949660 | 1.758908 |
| 0.06 | 0.324945 | 1.958670 | 5.598787 |
| 0.15 | 0.059902 | 1.943000 | 26.060267 |

Profiles average RGB over rows [120,240), across all 640 columns. RMS width is
the second moment about the fixed image center, so finite-sample tail events
affect it; it is not FWHM. Row sums are descriptive, not an energy acceptance
test or convergence proof. Three Python unit tests passed, including invalid
quantiles, missing/nonfinite/out-of-range data and failure to reproduce the
plateau/roughness controls. No new C++ build was needed for this test-only unit.

Conclusion: this fixture confirms loss of material roughness distinctions below
the current floor. It does not establish that the helmet reflection complaint
is solely caused by that floor. Work unit 4 remains partial: robust low-alpha
sampling/evaluation and physical-energy qualification are still outstanding.

Run with a fresh output directory (numpy and Pillow are required):

```powershell
python Tests/PathTracing/validate_near_mirror.py --output bin/PathTracingValidation/near-mirror-new-run
python -m unittest discover -s Tests/PathTracing -p test_near_mirror.py -v
```

The generated case folders contain editable scene.json/render-preset.json and
target.gltf files. Open one through Scene Editor for manual inspection if needed.
The linear.png files use a fixed [0,1] display scale without per-case normalization.

No roughness floor, BSDF, Hybrid or denoiser code is changed by this diagnostic.
Lowering alpha alone is not an accepted fix: near-normal float32 half-vector
sampling/evaluation loses angular precision, and true delta handling must also
preserve mixture PDFs and NEE/MIS bookkeeping. Numerical stability and energy
checks must precede a production change. The helmet's environment content and
final subjective reflection quality remain separate, unresolved qualifications.

## GGX Numerical Foundation (2026-10-31)

This is a deliberately separate implementation unit before lowering the alpha
floor or introducing a delta event. The production alpha floor remains 0.001,
and the denoiser VNDF helper is unchanged. Hybrid shading/IBL is not modified.

`PathTracingSampling.hlsli` now constructs NDF half-vectors by normalizing
`(alpha*sqrt(u)*cos(phi), alpha*sqrt(u)*sin(phi), sqrt(1-u))`, rather than
recovering a tiny sine from `sqrt(1-cosTheta*cosTheta)`. Azimuth is represented
in [-pi,pi), and precise intermediates prevent reassociation of the small height
residual. GGX evaluation and reflection PDF both obtain the distribution's
sin-squared term from `dot(cross(n,h),cross(n,h))`, avoiding loss through a
rounded `dot(n,h)` near one. The scalar-cosine overload remains for existing
direct formula tests, not the production BRDF/PDF path.

The new `Tests/shaders_GgxPrecision.hlsl` and `--ggx-precision` mode of the
existing D3D12 GPU test runner evaluate 28,672 samples: seven alpha values
(1e-6, 1e-5, 0.0000556767, 0.001, 0.01, 0.1, 1), four normal orientations,
and 1,024 stratified samples including 0 and 0.99999 endpoints. A double CPU
slope-angle reference checks half-vectors; axis-frame angular magnitude checks
specifically reject near-normal offset collapse. Distribution values are checked
against double evaluation of the returned quantized vectors. Independent
Fresnel/Smith cancellation checks the production BSDF weights, and a smooth
metal normal-incidence furnace check uses F0=0.8 with absolute tolerance 0.002.

Important scope: raw NDF direction/distribution probes can use alpha below the
production floor, but production BSDF weights still use the unchanged floor.
These tests are normal-incidence, not grazing-view, mixed dielectric, NEE/MIS,
or full low-alpha energy certification. Rotated world-space vectors near alpha
1e-6 remain ill-conditioned: D relative tolerances are 0.1% in axis frames,
2% for rotated alpha>=1e-5, and 12% for the rotated alpha=1e-6 diagnostic probe.
Those limits were specified before the first GPU run; this is not grounds to
enable alpha=1e-6 generally.

The first strict WARP test failed at alpha=0.01 / sample=0.99999 with half-vector
error about 2.18e-6. The real GPU passed the original 1e-6 bound. Inspection
isolated an azimuth component discrepancy consistent with WARP sin/cos precision;
neither precise intermediates nor angle wrapping alone removed that discrepancy.
The WARP-only direction bound was subsequently corrected to
`1e-6 + 0.0025*sin(theta_h)`, based on the documented Direct3D sin/cos absolute
error guarantee of 0.0008, rather than float-ULP accuracy. Hardware retains the
original 1e-6 bound. Unit-length, radial angular-offset, D/PDF and weight checks
were not relaxed. This contract correction is distinct from a renderer fix.

References consulted: [pbrt microfacet theory](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)
for distribution/visible-normal context and the warning about very smooth
numerical cases; [Microsoft sincos precision](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sincos--sm4---asm-)
for the software test's trigonometric error contract. This project retains its
own roughness-to-alpha mapping; pbrt's material parameter mapping was not copied.

Final validation evidence is under `bin/PathTracingValidation/ggx-stability-20261031/`:

- `hardware.log`: 28,672 samples, maximum half-vector error 1.91130e-7,
  maximum relative D error 0.0362727 (including the rotated alpha=1e-6 probe),
  current-floor furnace/weights passed; no D3D12 errors.
- `warp.log`: 28,672 samples, maximum half-vector error 2.29178e-6,
  maximum relative D error 0.0728729; same D/PDF/weight checks passed and
  the documented software trig contract passed; no D3D12 errors.
- `ctest.log`: 54/54 passed, including existing thin-transmission, volume,
  RR/NRD conversion and sampling contracts and the new GGX test.
- CMake Debug x64 final build: exit 0, zero warnings/errors, task
  `pt-ggx-stability-cmake-final-20261031` under the external agents directory.
- Normal vcxproj Debug x64 final build: exit 0, one existing MSB4011 warning,
  task `pt-ggx-stability-msbuild-final-20261031`.

`evidence.json` indexes the final source, test executable/CSO, application CSO
and raw log hashes; it is an evidence index, not an independent pass aggregator.
Image rerun: `bin/PathTracingValidation/near-mirror-stable-math-20261031/report.json`.
All five cases / 256 spp completed with no D3D12 errors/corruption; the three
below-floor images are still exactly equal. Profile RMS widths are 1.777437,
5.544276 and 26.060178 pixels for the below-floor plateau, 0.06 and 0.15 controls.
The old and new arithmetic need not produce bit-identical fixed-seed images;
this single-seed profile is not an independent energy-bias/convergence proof.
The diagnostic runner's three Python unit tests also passed. No user subjective
approval of the corrected helmet or the new sampling arithmetic is claimed.

No full Volume image suite or live RR/NRD quality/performance requalification is
claimed by CTest. Next, qualify grazing views and mixed-lobe/PDF/MIS energy
before changing the production floor; retain the exact-floor reflection fixture
as a baseline reproduction test, not as the desired final material response.
