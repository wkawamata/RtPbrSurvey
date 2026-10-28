# Native PT Volume Transmission Plan

Date: 2026-10-25
Status: Steps 1 to 3, Step 4a and single-convex glTF Volume verified. Unbiased internal traversal removes the fixed-scene grazing misses; user visual approval pending.
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

## Step 3 Distance Absorption (2026-10-26)

Step 2 was committed as `f66f3fb`. Step 3 removes only the finite colored-medium
gate: one closed Cube/Sphere, camera outside, smooth dielectric, full transmission,
white boundary tint/base color and IOR [1,4] remain the accepted runtime subset.
The runtime material layout remains 140 bytes.

The entering boundary supplies homogeneous medium absorption parameters. On every
subsequent segment inside that medium, the renderer computes the physical
boundary-to-boundary distance and multiplies throughput once by
`attenuationColor^(segmentLength/attenuationDistance)`. Internal reflection retains
the medium and contributes another segment; an exit does not apply a second
boundary absorption factor. Invalid attenuation inputs produce diagnostic code 6.

`Volume Transmittance` (saved enum 8, shader diagnostic 12) displays the product of
absorption factors along the sampled path. It excludes Fresnel, boundary tint and
background radiance. With accumulation its display is an average of path
transmittances, not attenuation evaluated at the mean path length.

### Quantity Contract

The first integration run passed the direct absorption controls but failed the
internal-reflection pair: an entry/exit count of 0.5 revealed that SceneColor held
multiple accumulated path samples. Applying Beer's law to that averaged distance
is not a valid test of the averaged transmittance. No GPU change or tolerance
relaxation was used to make this case pass.

Per-path tests now capture the existing `PathTracing.CurrentFrameRadiance`
resource with samplesPerFrame=1, the same seed, scene and sampling settings.
Only the diagnostic output differs between the paired distance/transmittance
captures. Their individual pixels are compared before any spatial averaging.
The stochastic Fresnel radiance case deliberately captures accumulated SceneColor
after 64 warm-up frames and compares an ensemble mean, not a particular path.
Warm-up frame counts are not used to claim an exact single-path SceneColor image.

The failed evidence and pre-correction runner are preserved in
`bin/PathTracingValidation/volume-absorption-20261026`.
The corrected report is `bin/PathTracingValidation/volume-absorption-r2-20261026/report.json`.

### Numerical Results

- [x] 18 absorption runtime cases passed: thickness 1/2, oblique incidence,
  uniform instance scaling, zero color channel, infinite attenuation distance,
  white medium, sphere, origin bias x100, direct transmittance, reflected/TIR
  path-length pairs and status controls, Fresnel radiance and NRD bypass.
- [x] Double-thickness normalized transmittance versus the square of the single:
  maximum error 0.000000240 (budget 0.002).
- [x] Origin-bias control: maximum difference 0.000000060 (budget 0.0005).
- [x] NRD request versus Native current-frame radiance: exact ROI equality.
- [x] Multi-segment transmittance errors: ordinary reflection 0.000000252,
  TIR 0.000000162 (both budget 0.002).
- [x] Fresnel/absorbing-slab radiance mean error 0.0004394 (budget 0.015).
- [x] CTest 44/44. Python 37/37 (17 transmission, 4 roughness texture, 5 MASK,
  11 volume tests). Debug build passed with two existing LNK4199 warnings.
- [x] No-absorption boundary runtime regression (12/12 cases).
- [x] Saved-scene captures for scenes 28, 29 and 30 completed.
- [ ] User visual approval of the saved-scene captures.

Boundary regression evidence is
`bin/PathTracingValidation/volume-absorption-boundary-regression-20261026/report.json`.
The three screenshot smoke reports are under
`bin/PathTracingValidation/volume-absorption-reviews-20261026`, each marked
`passed-runtime-smoke-not-subjective-approval`. Captures show the blue absorbing
box/sphere with refracted checker background. Sphere edge faceting and residual
sampling noise remain visible; screenshot inspection is not a quality sign-off.
The full thin/MASK/metallic GPU regression and NRD quality suite were not rerun
in this step; their CPU/contract tests are included in the counts above.

The independent reference uses IOR=1 to isolate absorption. The IOR=1.5 slab
reference also sums the repeated internal-reflection series per RGB channel:
`background * (1-F)^2 * T / (1-F^2*T^2)`, for the fixed parallel slab and disabled
environment. Sphere length uses the analytic chord, with a declared mesh/RAW
budget of 0.002; planar direct-color budget is 0.001. The paired-path tests require
matching scene and sampling settings and complete exits. Unit tests reject the
incorrect mean-distance method, missing controls and mismatched random seeds.

### Saved Scenes and Limits

- Scene28: absorbing box, thickness 0.8.
- Scene29: the same material and camera, thickness 1.6.
- Scene30: absorbing radius-1 sphere.

All use white boundaries, IOR=1.5, linear absorption color (0.25,0.5,0.8), distance
1 world unit, and the existing scene20 checker. Their descriptions record what
to observe in Japanese. NRD continues to fall back to Native PT.

This does not add alpha BLEND, rough volume BTDF, nested/overlapping media,
camera-inside support, glTF volume import or scattering. Shadow visibility rays
remain opaque; refracted-light connection/caustics-oriented sampling and colored
light projection onto a receiver are not qualified by these tests. Volume material
editing controls and glTF integration remain separate follow-up work units.

The capture contract is shared in `volume_capture.py`; it requires runtime scene,
resource, mode, sample settings, Debug Layer and NRD state rather than accepting
an image file alone. Reports retain exact case lists, assessment results,
relationship checks and source/binary/input/RAW/log hashes.

```powershell
python -B Tests/PathTracing/validate_volume_absorption.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/volume-absorption-new-run
```

## Step 4a: Volume Material Editor

Date: 2026-10-27.

- Existing Volume materials expose IOR (1 to 4), linear Absorption Color,
  positive Absorption Distance (world units, UI minimum 0.001), and an Infinite
  Absorption Distance checkbox. Switching from infinite to finite initializes
  the distance to 1; Undo restores the previous finite value.
- Edits use the existing BeginEdit/CommitEdit and preview rebuild path, preserving
  the current camera while refreshing material data and PT accumulation.
- Volume Base Color, Metallic and Roughness are read-only because this milestone
  qualifies only white, smooth, full-transmission dielectric boundaries.
- Creating/converting a material to Volume remains outside this UI increment:
  open saved scenes 27 to 30 and select their volume primitive to edit it.
- Session tests cover dirty tracking, grouped Undo/Redo, finite/infinite distance
  serialization and restoration. Interactive GUI behavior still needs user QA.
- glTF volume import remains Step 4b; no renderer or shader changes in Step 4a.
- Validation: Debug build succeeded (0 warnings/errors); CTest 44/44 passed,
  including the extended SceneEditorSession finite/infinite absorption tests.
  Build record: `C:/work/RtPbrSurvey-agents/volume-editor-build-20261027`.
  No interactive mouse/keyboard UI verification or new GPU image suite was run
  in Step 4a. Step 3 numerical evidence remains separate from this UI validation.

## Step 4b-1: glTF Volume Input Contract

Date: 2026-10-27.

`KHR_materials_volume` is inspected before the transmission/IOR early return.
Thickness defaults to zero, attenuation color to white, and omitted attenuation
distance to infinity per the Khronos specification. Explicit distance must be
positive, finite and representable in the float runtime; color must be a linear
RGB triple in [0,1]. Malformed objects and unsupported fields/textures are rejected.

Zero thickness, including an empty extension, preserves the existing thin path.
This subset also works when the extension is declared required. Nonzero thickness
is still rejected with a closed-mesh/medium-identity diagnostic, even without a
transmission extension; it is never silently converted to thin transmission.
This is input-contract groundwork, not glTF solid-volume rendering support.

The next increment must qualify closed consistently oriented triangle topology,
identify one connected medium across primitives/material boundaries, and reject
nested/overlapping or camera-inside cases before enabling volume transport for
imported meshes. Thickness factor is the thin/solid selector, not an extra length
multiplier for the ray-traced absorption distance. No GPU transport changes here.

Validation: Debug build succeeded with one existing LNK4199 test-link warning;
CTest 44/44 passed. Import tests exercise empty/zero-thickness required extensions,
thin-material preservation, malformed objects, color/type/range errors, invalid or
float-underflow distances, unsupported thickness textures, and solid rejection
with/without transmission. Both scene-asset and legacy loaders reject invalid
inputs without publishing stale mesh data. No new GPU image suite was run.
Build record: `C:/work/RtPbrSurvey-agents/volume-gltf-contract-build-20261027`.

## Step 4b-2: Closed-Shell Topology Gate

Date: 2026-10-27.

Added a renderer-independent triangle topology validator in
`Scene/VolumeMeshTopology.h`. It requires finite positions, valid triangle indices,
nonzero triangle area, exactly two oppositely oriented faces per edge, one
edge-connected shell, a connected incident-face fan at each vertex, and positive
signed enclosed volume (outward winding in the input coordinate system).
Signed-volume calculation uses double precision relative to an input point.

Vertices at exactly equal positions are welded for topology only, allowing UV and
normal seams without changing render vertices. No epsilon welding is performed:
nearby but distinct boundary vertices remain a crack and are rejected. The caller
must supply winding in the chosen renderer coordinate convention after conversion.

Tests cover outward/inward tetrahedra, split seam vertices, small translated
geometry, open boundaries, mixed winding, excess edge incidence, invalid indices,
degenerate/partial triangles, nonfinite positions, separate shells and shells
touching at one vertex.

This gate is not yet connected to glTF volume acceptance. It does not prove the
absence of triangle self-intersections, nor classify camera position or overlaps
with other scene objects. Those checks and instance/medium identity remain
required before removing the solid-volume import guard. GPU behavior is unchanged.

Validation: Debug build passed (one existing LNK4199 test-link warning), CTest
44/44 passed including the new topology cases in GltfNodeMesh. No GPU image suite
was rerun for this CPU-only gate. Build record:
`C:/work/RtPbrSurvey-agents/volume-topology-build-20261027`.

## Step 4b-3: Convex Shape and Placement Guards

Date: 2026-10-27.

The initial imported-volume target is narrowed to one convex closed shell.
`Scene/ConvexVolumeBounds.h` builds supporting face planes only after the topology
gate passes. All referenced vertices must be behind every outward face plane,
within a distance tolerance of 1e-6 times the largest bounds extent. It uses double
precision and rejects numerically thin solids whose vertex centroid is not
strictly inside every plane beyond that tolerance. Convex plane testing has a
16,777,216 face/unique-vertex pair budget and fails closed above it.

The resulting guard classifies finite world-space camera positions as Outside,
Inside or Boundary; invalid positions have a separate Invalid result. Only Outside
will be acceptable at integration. Positions within the tolerance band of a face,
edge or vertex are not considered safely outside. The caller must supply geometry
and camera positions in the same coordinate space with outward winding; transforms
and handedness conversion are not performed by this helper.

A conservative AABB overlap query proves separation only. Touching, overlapping,
inverted or nonfinite object bounds fail closed. This can reject some geometrically
separate objects whose AABBs overlap; it must not be presented as an exact triangle
intersection test. General concave/self-intersecting mesh support remains out of
scope, and these helpers are not yet called by the loader or renderer.

Tests cover exterior/interior/boundary/nonfinite cameras, scale and translation,
closed concave input that independently passes topology, numerically thin solids,
clear bounds separation, touching/overlapping/invalid bounds, and topology bypass.
The next integration must assign one medium to the qualified instance, transform
and revalidate geometry, enforce the scene/camera guards, and preserve the current
Native fallback policy before removing the glTF solid-volume rejection.

Validation: Debug build succeeded (one existing LNK4199 test-link warning), CTest
44/44 passed including the convex/camera/bounds cases. Build record:
`C:/work/RtPbrSurvey-agents/volume-placement-build-20261027`.
These are CPU-only tests; no new GPU capture or user visual approval is claimed.

## Step 4b-4: Single glTF Medium Integration

Date: 2026-10-27. Implementation added; validation results follow below.

Nonzero `KHR_materials_volume.thicknessFactor` now selects solid transport for one
convex closed mesh node, one primitive and one material. The loader rejects mixed
transmission, non-white boundaries, roughness/metallic, boundary textures, emission
and IOR above 4. Volume absorption is copied through both legacy and node-mesh
SceneBuilder conversion paths; thin and solid flags remain distinct. Thickness
factor selects solid behavior but never scales traced world-space distance.

The active draw instance is the medium identifier. An imported-volume scene guard
revalidates transformed geometry, rejects more than one medium or mixed boundary
material, and requires AABB separation from all other visible objects. It undoes
InstanceData's transposed matrix storage before world-space tests. Mirrored or
collapsed solids fail qualification rather than silently changing boundary winding.

SceneDocumentBuilder rejects unsupported placement and cameras inside/on the solid.
The engine also validates direct/legacy scene paths, rechecks the camera on camera
updates, and skips PT dispatch with a runtime status/fallback color if the camera
enters the unsupported region. Resource reload revalidates even same-pointer mesh
updates. NRD remains a reasoned Native fallback; no new shader/ABI layout is needed.

Scene31 is a small original embedded glTF tetrahedron with absorbing glass and a
separated checker background. Its material is asset-owned; primitive material UI
does not edit imported glTF values. The RAW runner captures matched distance,
transmittance and status at IOR=1 plus NRD bypass, checking Beer-Lambert consistency
per complete path. A separate four-halfspace tetrahedron reference also checks the
central 32x32 ROI's world-space path length at IOR=1. This is not an unrestricted
glTF volume/quality certification.

The previous Step 4b-1 to 4b-3 notes describe their historical guard-only states;
the single-convex subset above is the newly connected path. Concave/multi-primitive,
multi-material, nested/overlapping volumes, rough volume, internal camera, volume
textures and colored-shadow/caustics qualification remain outside this increment.

### Ray-Offset Silhouette Limit

The first integration run correctly failed its all-pixel status gate: 81 of
2,073,600 pixels reported missed exit (status 1, inside=1). Their analytic chord
lengths were at most 0.000202780, near the fixture's origin offset plus TMin floor
of 0.0002. This evidence remains in
`bin/PathTracingValidation/gltf-volume-integration-20261027/report.json`.
No shader behavior or offset defaults were changed to hide these pixels.

The revised test explicitly reports this known limitation instead of declaring
every pixel valid. It accepts only the missed-exit status in an analytically
bounded silhouette band: positive chord <= offset+TMin+floating-point margin,
or a slightly negative chord within that margin. Margin is 32*float32 epsilon*
max(1, maximum absolute camera coordinate). Any other status or missed exit on
a path thicker than that band fails. Unit tests inject an interior missed exit
and another status to prove they remain rejected. Interior geometric and absorption
budgets remain unchanged. A successful qualified-interior run with such pixels
is named `passed-gltf-volume-with-grazing-limit`, not an unqualified pass.

### Integration Evidence

- Final integration report:
  `bin/PathTracingValidation/gltf-volume-integration-r3-20261027/report.json`.
- Four GPU cases completed: distance, transmittance, status and NRD bypass.
- 187111 complete transmitted pixels longer than 0.05 world units assessed.
- Central ROI geometric path length maximum error: 0.0000141870 (budget 0.005).
- Per-path absorption maximum error: 0.0000001592 (budget 0.002).
- NRD request versus Native transmittance: exact equality.
- Known silhouette missed-exit count: 81; maximum analytic chord 0.000202780,
  origin-offset/TMin floor 0.0002 and explicit float margin 0.0000190735.
- The intermediate r2 captures completed, but report serialization failed on a
  NumPy scalar. JSON output was fixed and covered by a unit test before the r3
  rerun. Its evidence was not promoted to a successful report.
- Final Debug build: r3 succeeded, 0 errors and two existing LNK4199 test-link
  warnings. Record: `C:/work/RtPbrSurvey-agents/volume-gltf-integration-build-r3-20261027`.
  The initial incomplete-type ownership build failure was fixed before r2/r3.
- CTest 44/44 passed in the final rerun. The first saved-scene load failed because
  orthographicHeight was omitted; the required field was added without changing
  schema validation. Volume-related Python tests: 16/16 passed.
- Existing Cube/Sphere GPU boundary regression: 12/12 passed, report at
  `bin/PathTracingValidation/gltf-volume-boundary-regression-20261027/report.json`.
- Scene31 PNG smoke capture: 256 samples, passed runtime/settings/Debug Layer
  checks; output under `bin/PathTracingValidation/gltf-volume-review-20261027`.
  Image inspection shows colored glass and refracted checker background, with
  visible residual sampling noise. User quality approval has not been requested
  or recorded as complete. No full thin/MASK/metallic GPU or NRD quality suite was
  rerun in this increment; their CTest contracts passed.

## MSBuild Compatibility Fix

Date: 2026-10-28.

The regular vcxproj does not define NOMINMAX, unlike the CMake validation build.
Windows min/max macros broke six newly added std::min/std::max call sites in the
convex/placement headers and volume editor. Parenthesized standard function names
now follow the repository's existing macro-safe convention without changing global
project definitions. Regular RtPbrSurvey.vcxproj Debug x64 MSBuild succeeded:
0 errors, one vcpkg MSB4011 duplicate-import warning. Build record:
`C:/work/RtPbrSurvey-agents/volume-msbuild-minmax-fix-20261028`.
This is a compile compatibility change; no new GPU image suite was run.

## Step 4b-5: Preserve Thin Internal Segments

Date: 2026-10-28. Numerical validation complete.

For continuations still inside a qualified convex volume, start the ray at the
unshifted boundary position with TMin=0. Exclude the previous triangle of that
same instance, and reject same-medium candidates whose geometric normal does not
face along the outgoing ray (entry/coplanar self-hits). Foreign-instance candidates
are not filtered and still reach the existing unsupported-overlap diagnostics.
Outside-volume rays retain the prior origin offset and TMin policy.

This avoids stepping past a thin exit while keeping absorption measured from the
physical boundary positions. It applies to entry transmission and internal
reflection/TIR continuations; it is not a general nested/concave volume algorithm.
No Fresnel, throughput, absorption equation or material ABI changes were made.

The first GPU trial reduced the original 81 grazing missed-exit pixels to zero:
`bin/PathTracingValidation/gltf-volume-unbiased-20261028/report.json`.
The initial improvement target was <=8; after observing zero, the default final
regression budget was tightened to zero. The runner still has an explicit
`--max-grazing-pixels` override for historical control experiments, recorded in
each report, but acceptance uses the default 0.
Two added control captures multiply normalBias and rayTMin by 100 and require zero
path errors and exact transmittance equality to the ordinary settings.

The earlier 81-pixel report and its known-limit description above are historical
evidence, not a waiver for the new strict run. Zero errors in this fixed scene do
not certify every possible microscopic/grazing geometric configuration.

Final evidence:

- `bin/PathTracingValidation/gltf-volume-unbiased-final-20261028/report.json`:
  all 6 expected cases passed with grazingPixelBudget=0 and missed-exit count 0.
- Bias/TMin x100 transmittance difference: exactly 0; high-bias status also has
  no path errors or unfinished medium.
- 383 complete paths with lengths in (0,0.001] world unit: absorption maximum
  error 0.0000000772 (unchanged budget 0.002). Missing exit evidence, wrong
  absorption and invalid arrays are rejected by new unit tests.
- Interior geometric length error 0.0000141870, absorption error 0.0000001592,
  and NRD-bypass difference 0: unchanged from the original qualified interior.
- Boundary/TIR regression: 12/12 passed, report at
  `bin/PathTracingValidation/volume-unbiased-boundary-regression-20261028/report.json`.
- Absorption regression: 18/18 passed without relaxing tolerances, report at
  `bin/PathTracingValidation/volume-unbiased-absorption-regression-20261028/report.json`.
- CTest 44/44 and volume-related Python tests 19/19 passed.
- CMake validation Debug build succeeded (0 warnings/errors), record at
  `C:/work/RtPbrSurvey-agents/volume-unbiased-boundary-build-20261028`.
  No full thin/MASK/metallic GPU or NRD quality suite was rerun; their CTest
  contracts passed. The new query filtering is active only inside a volume.
- Regular vcxproj Debug x64 also succeeded (0 errors, one existing vcpkg MSB4011
  duplicate-import warning), record at
  `C:/work/RtPbrSurvey-agents/volume-unbiased-msbuild-20261028`.
  Its updated `bin/x64/Debug` shader was exercised by a 256-sample Scene31 PNG
  capture under `bin/PathTracingValidation/volume-unbiased-msbuild-review-20261028`.
  Runtime/settings and D3D12 error checks passed; visual inspection shows no gross
  regression, but user subjective approval is still separate.

## References

- [KHR_materials_volume](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume)
- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [KHR_materials_ior](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_ior)
- [PBRT dielectric BSDF and radiance transport](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)

The volume specification distinguishes surface tint from distance absorption,
requires closed volume boundaries, and recommends traced internal distance for
ray tracers. Project restrictions and work-unit boundaries above are our design,
not claims that all glTF volume combinations are already supported.
