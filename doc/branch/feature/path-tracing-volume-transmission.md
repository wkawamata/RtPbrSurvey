# Native PT Volume Transmission Plan

Date: 2026-10-25
Status: Step 1a mathematical helpers verified; scene data and runtime wiring not started.
Branch: `codex/pt-volume-transmission`.
Base: `e36933b`, dependent on PR #104 (not merged at plan creation).

## Goal

Preserve the existing thin-surface path and add a separate solid-volume mode.
Trace entry and exit boundaries, bend rays at each boundary, and attenuate light
using the actual distance traveled inside a homogeneous absorbing medium.
The eventual target is colored transmitted illumination through stained glass;
this first volume milestone does not claim caustics or colored direct shadows.

## Initial Scope

- Native PT only. NRD remains a reasoned Native fallback for volume materials.
- One isolated closed dielectric solid in air, camera outside, smooth surface,
  metallic=0 and transmission=1 for the first qualified implementation.
- Constant IOR, attenuationColor and attenuationDistance. No scattering.
- Explicit scene material mode: opaque / thin / volume. Existing saved thin
  scenes keep their meaning and defaults; do not reinterpret them as solids.
- No nested/overlapping media, open or non-manifold geometry, rough volume BTDF,
  dispersion, alpha MASK volume boundaries or camera-inside qualification yet.
- Unsupported combinations must be documented and diagnosed, not presented as
  validated volume rendering. Do not silently treat volume as thin transmission.

## Transport Contract

1. Retain the unflipped geometric orientation for entry/exit classification.
   The thin path's face-forward shading normal alone cannot identify the medium.
2. Use incident/transmitted IORs at each boundary, Snell refraction, dielectric
   Fresnel reflection and total internal reflection. Reflection keeps the medium;
   transmission alone changes it. Volume backfaces must remain traceable even
   when the glTF material is not doubleSided.
3. Track medium identity by instance and its parameters, not just material ID.
   Offset spawned rays onto the correct side. Include radiance-transport eta
   scaling consistently with event PDFs; verify air/solid/air cancellation.
4. Apply Beer-Lambert transmittance to every traveled segment in the medium,
   including segments after internal reflections. Distance is world-space and
   must not accidentally include ray-origin bias as physical absorption.
5. Use `T(length) = attenuationColor^(length / attenuationDistance)` with explicit
   zero-length, zero-color and infinite-distance handling. Boundary tint and
   distance absorption are separate; do not apply absorption twice at an exit.
6. A missing exit, medium mismatch or exhausted bounce budget must not invent
   an exit position. Expose a diagnostic/count and qualify the expected behavior.

For closed geometry, mesh dimensions define physical thickness. A UI thickness
parameter may generate/scale geometry later; it must not multiply the traced
internal distance. For glTF, positive thicknessFactor enables volume behavior;
the ray tracer uses traced distance rather than thicknessTexture for path length.
Default/zero thickness remains thin. glTF integration follows after core tests.

## Work Units

### 1. Data and Mathematical Contracts

- Add separate volume settings to scene/editor serialization and neutral material
  data, retaining defaults and backward compatibility. Add explicit validation
  for finite IOR, color bounds and positive attenuation distance.
- Implement independently tested smooth dielectric sampling and attenuation
  helpers before wiring them into the renderer. Define CPU/HLSL layout checks.
- Tests: normal/oblique Snell angles, critical angle/TIR, reflection/transmission
  probabilities, eta scaling, zero/double distance, RGB absorption, no-absorption.

### 2. Boundary Transport

- Connect candidate/backface handling and per-path medium state without changing
  thin/opaque behavior. Start with a parallel slab and closed sphere.
- Add debug outputs for entry/exit, current medium, internal segment length and
  accumulated transmittance. Keep failure evidence when diagnosing medium errors.
- Check a parallel slab's outgoing direction and lateral displacement against
  independent analytic values. Check internal reflection followed by an exit.

### 3. Absorption and Geometry Scale

- Use actual medium segment length for Beer-Lambert attenuation.
- Add saved scenes: clear slab, same material at two thicknesses, colored sphere,
  oblique slab and TIR control. Include Japanese observation descriptions.
- Use IOR=1 and known Unlit backgrounds to isolate absorption from Fresnel and
  refraction. Expected double-thickness transmittance is the square of single.
- Verify uniform/nonuniform instance scaling and mirrored orientation separately.

### 4. Scene Editor and glTF

- Expose volume mode, IOR, absorption color/distance and explanatory diagnostics.
- Import a qualified subset of KHR_materials_volume with transmission/IOR.
  Do not remove the current importer rejection before this path really exists.
- Validate closed-mesh assumptions deliberately, accounting for vertices split
  at UV/normal seams. Record what the validator can and cannot establish.
- Add round-trip and malformed-input tests. Preserve original glTF material data
  and distinguish supported properties from unsupported combinations.

### 5. Regression and Acceptance

- Re-run thin/opaque, rough transmission, metallic and MASK contracts and runtime
  comparisons. Run Debug Layer checks and inspect graph/resource bindings.
- Reports require exact case lists, numerical assessments, runtime settings and
  source/binary/shader/input/capture hashes. Missing evidence is not a pass.
- Request user visual approval only after analytic checks pass. Review refracted
  background displacement, thickness-dependent color and internal reflection.
- Keep NRD fallback explicit. New APIs/layouts require host and shader rebuilds.

## Deferred Milestones

- Rough refractive microfacet BTDF with consistent PDFs/Jacobians.
- Nested/overlapping media and camera-inside support with a real medium stack.
- Colored visibility approximations, explicitly distinguished from physical
  refracted direct-light paths; then a caustic-capable sampling strategy.
- Volume-compatible denoising is a separate qualification task.

## Step 1a Results (2026-10-26)

- [x] Independent `Shaders/VolumeTransmission.hlsli`, not included by runtime PT yet.
- [x] Smooth dielectric sampling with explicit incident/transmitted IOR, Fresnel,
  Snell refraction, TIR, discrete event probability and radiance eta-squared weight.
- [x] RGB Beer-Lambert absorption with zero-length and infinite-distance contracts.
- [x] Reject invalid vectors, IOR/event inputs and attenuation parameters.
- [x] GPU contract shader compared against an independent double-precision CPU
  reference: 28 cases x 1024 events, including both sides of the critical angle.
- [x] Debug build: zero warnings/errors. CTest 44/44, including WARP contract.
- [x] Hardware run: 28,672 samples passed, no D3D12 errors.
- [ ] Step 1b: scene/material data, serialization and layout contracts.
- [ ] Step 2 onward: actual closed-volume boundary tracing and medium state.

Normal and incident vectors must be unit length, with normal pointing into the
incident medium. Random event input is finite in [0, 1). The sampling result's
probability is a discrete delta-event probability, not a solid-angle density.
Reflection weight is 1; transmission radiance weight is (incidentIOR/transmittedIOR)^2.
The Fresnel selection factor is already divided out and must not be multiplied
again by the caller. Thin transmission retains its existing behavior.

Absorption requires finite RGB in [0,1], finite nonnegative segment length and
positive attenuationDistance (positive infinity means no absorption). Zero length
returns white, including zero-color channels; positive length with a zero-color
channel returns zero for that channel when attenuationDistance is finite.
Boundary tint, medium identity and geometry distance are not implemented here.

The test uses 7 IOR pairs and 4 angle configurations, 7 absorption parameter
sets and 4 lengths. Direction/Fresnel/PDF/weight tolerances are fixed at
0.0002 * (1 + abs(reference)); event-frequency absolute tolerance is 0.002.
Invalid cases, flags and discrete selections are also checked. This is not a
rendered-glass or medium-tracking validation, and not a material import claim.

```powershell
ctest --test-dir bin/PathTracingValidation/thin-gltf-build-20261024 -C Debug --output-on-failure
& bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe bin/PathTracingValidation/thin-gltf-build-20261024/Tests/shaders_VolumeTransmission_CSMain.cso --volume-transmission --hardware
```

Build log: `C:\work\RtPbrSurvey-agents\volume-math-build-20261026\build.out.log`.
Generated test binaries and CTest logs remain outside version control.

## References

- [KHR_materials_volume](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume)
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [KHR_materials_ior](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_ior)
- [PBRT dielectric BSDF and radiance transport](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)

The volume specification distinguishes surface tint from distance absorption,
requires closed volume boundaries, and recommends traced internal distance for
ray tracers. Project restrictions and work-unit boundaries above are our design,
not claims that all glTF volume combinations are already supported.
