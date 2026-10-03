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
- [ ] Finish imported material-factor handling and unsupported-input diagnostics.

Step 4 is not a declaration of full glTF material compliance. The geometry-transform
portion is validated; remaining importer/material gaps are listed below before advancing
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
| Normal map, UV0, supplied tangents, default scale=1 | Native PT guide tested |
| Missing tangents | PT ignores the normal map; GBuffer uses an arbitrary fallback frame |
| `normalTexture.scale` / alternate texture coordinate sets | Not carried by the current material API |
| `baseColorFactor` | Loader reads it, but SceneBuilder does not carry it to the GPU material |
| glTF `emissiveFactor` | Not carried by the current loader/material path |
| Alpha MASK / BLEND / transmission | Not supported: BLAS geometry is opaque |
| `doubleSided` material semantics | Not supported: PT primary/continuation rays cull back faces |
| Interleaved, sparse, normalized integer attributes / non-triangle modes | Outside current packed-float importer contract; no new coverage |
| Runtime negative instance scale, singular/extreme scales | Not validated by this campaign |
| Skinning, morph targets, animated deformation | Not covered |

Opaque-only behavior must not be presented as faithful rendering of transparent,
cutout, or double-sided assets. Zero-scale geometry is outside the correctness guarantee;
the shader's degenerate normal fallback only prevents normalization of a zero vector.
GBuffer transform changes compile successfully but were not separately numerically
captured; the Forward shading path was not changed.

The glTF specification defines normal textures as linear tangent-space data, applies
normal scale to XY, and distinguishes opaque/mask/blend and double-sided material
behavior. These requirements motivate the exclusions above:
[Khronos glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc).

## Next small steps

1. Carry base-color and emissive factors, and normal texture scale through the neutral
   scene/GPU material contract. Add independent Albedo/Emissive/Normal guide tests,
   including shared texture references with different factors.
2. Make unsupported glTF features visible in import diagnostics. Do not silently
   advertise arbitrary glTF compliance; decide whether to reject or warn per feature.
3. Add GPU multi-mesh / different-material fixtures and a GBuffer/PT surface-input
   comparison. Treat negative runtime instance scale as a separate winding test.
4. Close Step 4 within the declared opaque subset, then proceed to Step 5 emission MIS.

## Reproduction

```powershell
cmake --build build --config Debug --target RtPbrSurvey.GltfNodeMeshTests
ctest --test-dir build -C Debug --output-on-failure
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/validate_geometry.py --output bin/PathTracingValidation/geometry-repeat
```

Use the freshly built Debug application. Python requires NumPy and Pillow. Generated
glTF/scene/preset/PTBUF/log files stay under ignored `bin/PathTracingValidation`.
The retained summary is `path-tracing-validation-results/completion-step-4-geometry-summary.json`.
