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
