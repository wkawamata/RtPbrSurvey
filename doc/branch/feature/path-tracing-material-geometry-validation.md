# Path tracing Step 4: material and geometry support

Date: 2026-10-03
Branch: `codex/pt-correctness-completion`
Base: `f3dd371`

## Status

- [x] Audit glTF / instance / ray-query surface data paths.
- [x] Correct normal and tangent transforms for rotation and nonuniform scale.
- [x] Correct baked glTF mirrored-node handedness and front-face winding.
- [x] Make glTF matrix and TRS node transforms agree.
- [x] Add CPU regression fixtures and native GPU normal-map / instance checks.
- [x] Record supported inputs and explicit exclusions.
- [x] Carry imported base-color/emissive factors and normal-map scale to the GPU.
- [x] Add unsupported-input diagnostics and reject unsafe packed-data inputs.

Step 4 is not a declaration of full glTF material compliance. The geometry-transform
portion and imported material factors are validated; remaining importer gaps are listed below before advancing
to emissive-surface sampling in Step 5.

## Changes

`SurfaceTransform.hlsli` implements normalized inverse-transpose normal transformation
with cofactors, without a per-hit matrix inverse. `SceneRayQuery.hlsli` uses it for PT
and shared ray-query normals. Tangents use the forward transform and Gram-Schmidt
orthogonalization; the tangent frame includes instance determinant handedness.
GBuffer uses the same normal transform and handedness calculation.

The CPU glTF loader now transforms tangents with the forward node transform, not the
normal inverse transpose. It includes the node determinant sign in tangent `w` and
accounts for a baked negative determinant when converting triangle winding to LH.
Matrix nodes now use DirectX row-vector storage consistent with the existing TRS path.

No material-buffer layout, estimator, TLAS descriptor policy, alpha intersection,
or denoiser behavior is changed here.

## Validation

Debug x64 MSBuild succeeded, with the existing MSB4011 duplicate vcpkg import warning.
CMake built the glTF test target and HLSL shaders; the CPU-only executable emitted the
existing LNK4199 unused Streamline delay-load warning. CTest passed 24/24 tests.
Python passed 55/55 tests.

Four native PTBUF captures on RTX 2080 Ti, driver 616.56, tested:

| Case | Checks | Maximum absolute error |
| --- | --- | --- |
| Rotated, nonuniformly scaled surface | Normal and roughness | 0.0004475081 |
| Same surface with a linear RGB normal map | Normal and roughness | 0.0001269531 |
| Mirrored glTF node with normal map | Normal and roughness | 0.0001269531 |
| Shared glTF mesh in two differently rotated instances | Both instance regions | 0.0001318594 |

Threshold: 0.001 for native RGBA16_FLOAT normal/roughness values, selected in the test
before capture. The independent NumPy oracle uses `inverse(transform).transpose()`
and an explicitly constructed tangent frame, not the shader cofactor implementation.
Each region requires at least 1000 visible hit pixels, compares all hit values, and
rejects nonfinite values, missing captures, nonzero process exits, or D3D12 errors.
All four cases passed; ERROR/CORRUPTION count was zero. Existing InitialState buffer
warnings remain. This is an input-normal validation, not a rendered-energy reference.

The first pilot used an invalid absolute SceneDocument asset path and was terminated;
it is not included as a passing capture. The corrected fixtures use relative paths.

## Input support and exclusions

| Input | Current status |
| --- | --- |
| Indexed triangle geometry, packed float POSITION/NORMAL/UV0/TANGENT | Used by the tested path |
| Hierarchical glTF TRS / matrix nodes | CPU regression tested |
| glTF baked negative-determinant node | CPU and native PT guide tested |
| Positive nonsingular nonuniform instance scale + rotation | Native PT guide tested |
| Same mesh in multiple instances | Native PT guide tested |
| Multiple mesh ranges and material-ID remapping | Existing CPU tests; no new GPU multi-mesh campaign |
| Normal map, UV0, supplied tangents, scale=1 / 0.35 / 1.7 | Native PT guide tested |
| Missing tangents | PT ignores the normal map; GBuffer uses an arbitrary fallback frame |
| `normalTexture.scale` | Carried through loader, scene, GPU material and PT/GBuffer sampling |
| Alternate texture coordinate sets | Not carried by the current vertex/material API |
| `baseColorFactor` | Carried to GPU; native PT textured and factor-only cases tested |
| glTF `emissiveFactor` | Carried to GPU; textured and factor-only emission tested |
| Alpha MASK / BLEND / transmission | Not supported: BLAS geometry is opaque |
| `doubleSided` material semantics | Not supported: PT primary/continuation rays cull back faces |
| Interleaved, sparse, normalized integer attributes / non-triangle modes | Outside current packed-float importer contract; no new coverage |
| Runtime negative instance scale, singular/extreme scales | Not validated by this campaign |
| Skinning, morph targets, animated deformation | Not covered |

Opaque-only behavior must not be presented as faithful rendering of transparent,
cutout, or double-sided assets. Zero-scale geometry is outside the correctness guarantee;
the shader's degenerate normal fallback only prevents normalization of a zero vector.
GBuffer transform and factor changes compile successfully but were not separately numerically
captured. Forward receives the base-color multiplier but retains its existing lighting/color-space path;
its normal transform and normal mapping were not changed.

The glTF specification defines normal textures as linear tangent-space data, applies
normal scale to XY, and distinguishes opaque/mask/blend and double-sided material
behavior. These requirements motivate the exclusions above:
[Khronos glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc).

## Next small steps

1. Add separate-BLAS GPU multi-mesh fixtures and a GBuffer/PT surface-input
   comparison. Treat negative runtime instance scale as a separate winding test.
2. Close Step 4 within the declared opaque subset, then proceed to Step 5 emission MIS.

## Import diagnostics (2026-10-04)

The shared glTF model-load path now inspects inputs before the importer accesses raw
attribute/index pointers. Both flattened `LoadGltfMesh` and CPU `LoadGltfSceneAsset`
use this policy. Each diagnostic has a Warning/Error severity, stable code, asset
location and explanation. `GltfSceneAssetLoadResult::diagnostics` retains them;
`LoadGltfMesh` has an optional diagnostic output parameter. Rejected model data returns
`UnsupportedData`, an invalid asset and an explanatory message. The legacy loader
clears output on entry, preventing stale mesh data from surviving a failed load.

Diagnostics are emitted once per load to `OutputDebugStringA` (Visual Studio Output)
and stderr (console/helper logs). No new ImGui window is introduced. `-LogToFile` is
the D3D12 message log and is not a persistence mechanism for these importer messages.

| Policy | Inputs |
| --- | --- |
| Warn and import core/static approximation | MASK/BLEND, double-sided, UV sets other than UV0, texture/material extensions, optional extensions, custom samplers, morph targets, skinning, animations, ignored attributes, missing normals/tangents/UV0 |
| Reject before raw conversion | Required extensions, non-indexed/non-triangle primitives, wrong/normalized/sparse attribute types, mismatched counts, interleaving, empty/unaligned/out-of-bounds accessor data, nonfinite float attributes, invalid index type/count/range, invalid material/texture/image/node/mesh/scene references, cyclic node graphs |

Warnings do not mean feature compliance: for example, a MASK material still renders
opaque, double-sided still uses ray back-face culling, and UV1 samples UV0. A missing
normal uses the existing fixed fallback; a missing tangent retains the documented
PT/GBuffer behavior. Required extensions fail rather than silently substituting a
core approximation. Sampler overrides are warned because one shared renderer sampler
is used. This is an importer-contract check, not a complete Khronos schema validator.

The CPU test mutates a supported glTF into 26 warning/rejection variants, including
sparse and interleaved attributes, NaN positions, invalid triangle indices, and a
node cycle. Warning cases must load and expose the expected code/location; error
cases must fail through both APIs without stale output. Supported opaque packed-float
input must remain diagnostic-free.

Validation on 2026-10-04: Debug x64 MSBuild and the full CMake Debug build succeeded,
CTest passed 24/24 tests (including all 26 diagnostic variants), and Python passed
60/60 tests. DamagedHelmet loaded and converted successfully; sampler overrides
and its missing tangents produced the expected warnings. The five native material
captures were repeated after the validator change and all passed, with zero D3D12
ERROR/CORRUPTION lines and unchanged capture hashes relative to the material campaign.
The new source/executable snapshot is retained in
`path-tracing-validation-results/completion-step-4-import-regression-summary.json`.

## Material-factor follow-up

Base: `4ee6691`. The GPU material stride is now 92 bytes (previously 60). Color factor,
emissive factor and normal scale offsets are 60, 76 and 88; CPU static assertions and
shared HLSL declarations guard the layout. Rebuild the renderer and all material-consuming
shaders together; cached old shaders are incompatible with the new structured-buffer stride.

SceneMaterial defaults remain unit factors and scale=1 to preserve procedural scenes.
Imported glTF emissive factors default to zero as specified. A glTF material that has
nonzero emission but no emission texture receives one shared white texture per loaded
asset, so it does not multiply its factor by the missing-emission black fallback.
Normal-map scale multiplies tangent-normal XY before world-space normalization.
Base-color and emission factors multiply decoded linear RGB in PT/GBuffer; no texture
is modified or duplicated to bake a per-material color factor.

`validate_materials.py` uses two glTF meshes with two materials that share one 1x1 texture.
The loader flattens them into one mesh while preserving primitive material IDs. It captures
NormalRoughness, Albedo, emission debug HDR, factor-only emission, and factor-only Albedo.
The oracle derives standard sRGB decoding, linear factors and independently transformed
tangent frames. It removes a two-pixel boundary from the hit mask, requires 1000 pixels
per material and retains both material regions separately. PT guide alpha is hit coverage,
not the imported material alpha; alpha-mask/blend rendering is still excluded.

Debug MSBuild and the complete CMake Debug build succeeded. CTest passed 24/24 tests;
Python passed 60/60 tests. Native material tests use RTX 2080 Ti / driver 616.56.
Results are retained in `path-tracing-validation-results/completion-step-4-materials-summary.json`.
These debug/input tests do not establish unbiased emissive-surface transport or light sampling.

All five material captures passed with zero D3D12 ERROR/CORRUPTION lines:

| Capture | Maximum absolute error |
| --- | --- |
| Normal scale 0.35 / 1.7 | 0.0003344794 |
| Shared-texture Albedo with different factors | 0.0003593340 |
| Shared-texture emission with different factors | 0.0000001826 |
| Factor-only emission | 0.0000000238 |
| Factor-only Albedo | 0.0003906250 |

The existing procedural input-plane regression also passed: NormalRoughness maximum
error 0.0004882813 (limit 0.001), Albedo 0.0004450518 (limit 0.0015), with zero D3D12
errors. This confirms the unit SceneMaterial defaults for the tested fixture, not a
whole-scene visual equivalence guarantee. Its report is retained as
`path-tracing-validation-results/completion-step-4-existing-inputs-summary.json`.

## Reproduction

```powershell
cmake --build build --config Debug --target RtPbrSurvey.GltfNodeMeshTests
ctest --test-dir build -C Debug --output-on-failure
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/validate_geometry.py --output bin/PathTracingValidation/geometry-repeat
python -B Tests/PathTracing/validate_materials.py --output bin/PathTracingValidation/materials-repeat
```

Use the freshly built Debug application. Python requires NumPy and Pillow. Generated
glTF/scene/preset/PTBUF/log files stay under ignored `bin/PathTracingValidation`.
The retained summary is `path-tracing-validation-results/completion-step-4-geometry-summary.json`.
