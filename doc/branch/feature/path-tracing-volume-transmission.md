# Native PT Volume Transmission Plan

Date: 2026-10-25
Status: design; implementation not started.
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

## References

- [KHR_materials_volume](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume)
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [KHR_materials_ior](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_ior)

The volume specification distinguishes surface tint from distance absorption,
requires closed volume boundaries, and recommends traced internal distance for
ray tracers. Project restrictions and work-unit boundaries above are our design,
not claims that all glTF volume combinations are already supported.
