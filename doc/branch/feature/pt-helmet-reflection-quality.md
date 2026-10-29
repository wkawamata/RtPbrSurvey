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

## First Findings

- DamagedHelmet uses one opaque metallic-roughness material, `Material_MR`.
  Its visor-like region is not an explicit transmission/glass material.
- It has a normal texture but its primitive contains POSITION, NORMAL and
  TEXCOORD_0 only; no TANGENT attribute.
- `LoadCommittedHitMappedNormal` in `Shaders/SceneRayQuery.hlsli` returns the base
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

## Next Work Units

1. Generate missing UV-based tangents using MikkTSpace, with CPU tests for mirrored
   UVs, seams, invalid/degenerate geometry and existing tangent preservation.
   Keep Hybrid shading/IBL behavior unchanged; review any shared import impact
   explicitly before adopting an integration path.
2. Re-run original/no-normal A/B, add normal-guide evidence and inspect the actual
   helmet. Require a correct change, not simply any nonzero image difference.
3. Examine metallic/roughness texture values and environment sampling only after
   normal mapping works. Use isolated controls rather than tuning to Hybrid.
4. Compare Native accumulation with RR at matched camera/material/environment.
   Denoising must not conceal a Native material integration failure.

The local scene10 user edit and existing generated/untracked files are retained
and excluded from this work. Local main's pre-existing divergent planning commit
is untouched; this branch starts directly from origin/main.
