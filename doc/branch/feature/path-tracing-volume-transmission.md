# Native PT Volume Transmission Plan

Date: 2026-10-25
Status: Steps 1a, 1b and the initial Step 2 boundary subset verified. Distance absorption is not active yet.
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
- [x] Step 1b: scene/material data, serialization and layout contracts.
- [x] Step 2: closed-volume boundary tracing and medium state for the initial subset.
- [ ] Step 3 onward: distance absorption, wider scene coverage and later material integration.

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

## Step 1b Data and Layout (2026-10-26)

Scene materials now distinguish opaque / thin / volume. Volume mode requires an
explicit absorption block; it is not a synonym for alpha blending. Old opaque
and thin documents retain their defaults and serialized shape.

```json
"transmission": {
  "mode": "volume",
  "factor": 1.0,
  "tint": [1.0, 1.0, 1.0],
  "ior": 1.5,
  "volume": {
    "attenuationColor": [0.2, 0.5, 1.0],
    "attenuationDistance": 2.0
  }
}
```

Absorption color is linear RGB in [0,1]. Distance is positive world-space length;
JSON null explicitly denotes positive infinity (no absorption). Numeric values
that overflow float are rejected rather than converted into no absorption.
Missing/wrong-type/unknown volume fields and volume blocks in thin/opaque mode
are rejected transactionally. Non-volume modes cannot silently discard modified
absorption settings during save. No synthetic physical thickness field is added.

`Shared/VolumeAbsorptionParameters.h` and its HLSL counterpart define a standalone
16-byte parameter block: RGB at offset 0, distance at offset 12. CPU size/offset
assertions and a GPU root-constant transfer test verify this contract. The GPU
test computes absorption from the uploaded values, rather than a duplicate set
of shader constants, for four of the existing angle/length cases.

This block is not appended to the runtime Material yet: that buffer stays at
124 bytes until Step 2 connects the actual transport path. The SceneDocument
builder explicitly rejects volume mode before mutating a previously built scene.
The editor labels externally loaded volume data as not connected and does not
put its enum value through the existing two-item opaque/thin mode selector.
There is not yet a GUI action for creating a volume material.

Tests cover JSON round trips, file save/reload, infinite-distance encoding,
malformed/range/overflow inputs, old scenes and the unsupported-runtime gate.
Full rendered-image regression is deferred until the transport path is wired.

Validation: Debug build succeeded. The initial rebuild had three existing
LNK4199 Streamline delay-load warnings; final incremental build had zero warnings
and errors. CTest 44/44 passed. WARP and hardware both passed 28,672 volume
contract samples, now including absorption from the CPU-uploaded parameter block.
No D3D12 errors were reported by the GPU test. This is not runtime volume image
validation. Build logs are under `C:\work\RtPbrSurvey-agents\volume-data-build-r2-20261026`.

The preceding mathematical work unit was committed as `b28721a`; Step 1b is a
separate change set. The runtime Material stride remains unchanged and the
glTF volume extension is still rejected until a real integration is implemented.

## Step 2 Boundary Transport (2026-10-26)

Step 1b was committed separately as `2effeea` before starting this integration.
The currently accepted runtime subset is a single visible Cube or Sphere
primitive, camera outside, metallic=0, roughness=0, transmission=1, white base
color/tint and IOR in [1,4]. A finite colored absorption setting is explicitly
rejected until Step 3; a no-absorption setting has no optical effect to omit.
Open primitives, multiple volume instances and an initial camera inside the
primitive are rejected before replacing the previously built scene.

The runtime Material now has 140 bytes (formerly 124), with the 16-byte absorption
block at offset 124. Host binaries and all shaders must be rebuilt together.
The standalone glTF volume importer still rejects the extension; this step does
not change its accepted material combinations.

The PT path keeps an instance identity and IOR while inside a volume. It classifies
entry/exit before face-forwarding the geometric normal. Reflections retain the
medium; transmissions alone change it. Volume backfaces remain candidates even
when doubleSided is false. Invalid medium transitions and missing exits do not
fabricate a successful exit.

Ray origins are moved along the outgoing volume direction. Internal distance is
measured between the physical boundary hit positions, rather than summing biased
RayT values, and includes segments ending in internal reflections. This distance
is diagnostic only in Step 2; Beer-Lambert multiplication follows in Step 3.
Volume delta events disable competing opaque NEE/MIS state and use the tested
radiance eta-squared weight. NRD conversion/evaluate and presentation are gated
with `Native fallback: volume transmission is not qualified for NRD`.

Four diagnostic outputs are available from Path Tracing Output and saved presets:

- Volume Boundaries (enum 4): entry count / exit count / total internal distance.
- Volume Status (enum 5): error code / still inside / internal reflection count.
- Volume Outgoing Direction (enum 6): last direction returned to air, including
  direct front-surface reflection.
- Volume Exit Position (enum 7): last transmitted exit position, zero if absent.

Error codes: 1=miss while inside, 2=medium/instance mismatch, 3=bounce budget,
4=invalid boundary sample, 5=unexpected backface while outside. Accumulated
diagnostics are sample averages, not per-frame global error counters. Camera
motion into the solid remains unqualified and produces code 5, not an invented
initial medium. Signed directions/positions require RAW inspection.

The runtime suite fixes its budgets before execution. It checks Snell entry,
parallel-slab exit direction and position, traveled slab distance, sphere chord
length, reflection state, no-absorption radiance, NRD bypass and a deliberately
insufficient bounce budget. A separate cube orientation forces a side-boundary
TIR before an exit. Exact case names and runtime state are required in reports.

The first capture passed its numerical test but report serialization failed on a
NumPy integer in the ROI. ROI bounds were converted to native Python integers;
the numerical criteria were unchanged, and a JSON serialization regression test
was added. Original artifacts remain in `bin/PathTracingValidation/volume-boundary-20261026`.
The rerun output is `bin/PathTracingValidation/volume-boundary-r2-20261026`.

Final results:

- [x] Debug x64 CMake/MSBuild build. Final incremental build: zero warnings/errors.
  The first full rebuild had five existing Streamline delay-load LNK4199 warnings.
- [x] CTest 44/44. Python 31/31 (17 transmission, 5 volume, 4 roughness texture,
  5 alpha MASK tests). The original .vcxproj dependency edits were XML-checked;
  the build used the existing CMake-generated serial ALL_BUILD project.
- [x] Volume RAW runtime 12/12. No D3D12 ERROR/CORRUPTION in captured logs.
- [x] Prior thin/opaque runtime regression: 9 captures passed, including exact
  opaque equality to the old matched executable/shader pair.
- [x] Roughness texture runtime regression: 6/6 passed.
- [x] Scene27: 1920x1080, 256 samples, 8 bounces, screenshot captured.
- [ ] User visual approval. Full alpha MASK/metallic runtime suites and NRD image
  quality suites were not rerun in this step; their contract tests passed.

Maximum ROI errors: slab length 0.000973; entry direction 0.00000141;
exit position 0.000244; outgoing-direction mean 0.001911; sphere chord 0.001970;
radiance mean 0.000999. All stayed within the predefined budgets in the runner.
The ordinary internal-reflection control averaged 0.04111 reflections, while
the side-boundary TIR case averaged 0.99966. Both ended outside with zero error
in the checked ROI. The budget control returned exactly [3,1,0]. These are ROI
results, not a guarantee that every path at all grazing angles terminates within
an arbitrary user bounce budget.

Local evidence:

- `bin/PathTracingValidation/volume-boundary-r2-20261026/report.json`
- `bin/PathTracingValidation/volume-thin-regression-20261026/report.json`
- `bin/PathTracingValidation/volume-roughness-regression-20261026/report.json`
- `bin/PathTracingValidation/volume-boundary-review-20261026/report.json`
- `bin/PathTracingValidation/volume-boundary-review-20261026/27-volume-smooth-boundary.png`

Numerical evidence hashes the executable, PT shader, runner, fixture and generated
inputs/RAW/logs. Build log folder:
`C:\work\RtPbrSurvey-agents\volume-boundary-build-r2-20261026`.
Generated evidence and screenshots are not committed or published.

Saved scene27 is `Assets/Scene/PathTracingValidation/27-volume-smooth-boundary/scene.json`.
It provides an oblique clear box and the existing scene20 checker background.
User visual approval is separate from numerical validation.

```powershell
python -B Tests/PathTracing/validate_volume_boundary.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/volume-boundary-new-run
```

## References

- [KHR_materials_volume](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume)
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [KHR_materials_ior](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_ior)
- [PBRT dielectric BSDF and radiance transport](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)

The volume specification distinguishes surface tint from distance absorption,
requires closed volume boundaries, and recommends traced internal distance for
ray tracers. Project restrictions and work-unit boundaries above are our design,
not claims that all glTF volume combinations are already supported.
