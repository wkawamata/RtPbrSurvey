# Path tracing Step 5: emissive-surface sampling and MIS

Date: 2026-10-04
Branch: `codex/pt-correctness-completion`
Base: `f9a2724`

## Progress

- [x] Audit the current BSDF-hit emission path.
- [x] Add a constant, opaque, one-sided rectangle-emitter fixture.
- [x] Add independent area quadrature and reference unit tests.
- [x] Complete the BSDF-only native GPU baseline and emission-off control.
- [x] Build an instance-aware CPU emissive-triangle sampling table.
- [x] Define GPU record serialization and compile sampling/PDF helpers.
- [x] Upload the table per frame and bind it to the PT pipeline.
- [ ] Invoke GPU sampling/PDF lookup from TracePath.
- [ ] Add area-sampled NEE with finite endpoint visibility.
- [ ] Add matching BSDF-hit MIS and explicit sampling modes.
- [ ] Validate mean agreement, occlusion, multiple emitters and convergence.

Step 5 is in progress. No emissive NEE/MIS shader implementation is enabled yet.
Current shaders add emission only on a BSDF hit, including directly visible primary hits.
Environment NEE/MIS and analytic scene lights remain separate techniques.

## Baseline

`Tests/PathTracing/validate_emissive.py` derives a scene with a neutral receiver
and a 3x3 rectangle at world Y=3, pointing down. Emission RGB is (0.8,0.4,0.2),
provided by glTF emissiveFactor without an emission texture. The receiver has
metallic=0, roughness=0.8 and nominal linear Albedo=0.5, including the procedural
texture's sRGB8 encode/decode. Direct and environment lighting are disabled;
skybox is disabled, and the two-segment BSDF path limit is fixed. No RR or denoiser.

The CPU reference integrates the same single-scattering Lambert/GGX BRDF over
emitter area using Gauss-Legendre orders 64/128 and nine representative positions
in the central 64x64 ROI. It includes receiver cosine, emitter cosine and inverse
distance squared. This is independent of shader sampling/PDF code, but shares the
declared material and camera model; it is not an arbitrary-asset ground truth.

Four independent seeds are compared with the reference. The predeclared test
uses a 2% relative discrepancy plus four seed-level standard errors. Uncertainty
above 5% or quadrature change above 0.2% is inconclusive, not passed. An emission-off
control must remain below 1e-7. These are diagnostic bands, not confidence intervals
or a proof of unbiasedness. Generated fixtures/captures/logs stay under ignored bin/.
An initial attempt stopped because lighting-enable flags were absent from the
capture log; it is not accepted as a passing baseline. Three flags have been added
to the actual runtime diagnostics before the verified run.

The verified 64 spp campaign used seeds 11/23/37/53 on RTX 2080 Ti, driver 616.56.
Measured mean was 0.0555422220 against the independent reference 0.0557248885,
relative difference 0.3278%. The four-standard-error diagnostic band was 1.0606%.
The quadrature relative change was below 4e-15. The emission-off control maximum
was zero. All five captures completed without D3D12 ERROR/CORRUPTION lines.
Debug x64 MSBuild and CMake Debug builds succeeded; Python passed 70/70 tests.
The result is retained in
`path-tracing-validation-results/completion-step-5-emissive-baseline-summary.json`.
This validates the existing two-segment BSDF-only baseline, not yet the new NEE/MIS path.

## GPU integration contract

1. Use an instance-aware table of emitting triangles. Identity is
   (instance ID, primitive index); world-space area must include nonuniform scale.
   Distinct instances of a shared BLAS are distinct light candidates. Resolve material
   IDs with the same vertex/instance rule as SceneRayQuery, including mesh-range offsets.
2. Include every potentially emitting opaque, lit triangle with finite positive area.
   A textured emitter may contain black texels; sampling a black location is a zero
   contribution, not grounds for excluding the entire triangle. Unlit surfaces stay
   on their current BSDF-only path until their emission/albedo semantics are defined.
3. Initially select triangle i by p_i = A_i / totalArea, then sample uniform area
   barycentrics with the square-root transform. Area PDF is p_i/A_i. Evaluate emission
   at the sampled UV using the existing linear texture/factor pipeline. No power-weighted
   distribution or ReSTIR is required for this first estimator.
4. Convert to solid-angle PDF: p_light = (p_i/A_i) * distanceSquared / cosEmitter.
   Use the geometric emitter normal for the one-sided cosine. Nonpositive cosine,
   degenerate area, zero distance or nonfinite values produce an invalid candidate.
   Selection probability must occur exactly once in the contribution and MIS PDF.
5. NEE visibility uses a segment from the same offset receiver origin used for BSDF
   continuation to the sampled emitter point. The sampled endpoint must not shadow
   itself, but intervening geometry must still occlude. Preserve finite ray distance;
   do not use a fixed world-unit endpoint subtraction without scale/thin-geometry tests.
6. For one NEE and one BSDF sample, use the power heuristic with their full directional
   PDFs. NEE evaluates the mixture BSDF PDF, not only the sampled lobe. On a BSDF hit,
   look up the triangle's selection probability and compute the same emitter PDF from
   the previous continuation origin. Primary emission has weight 1. Non-table emitters
   retain weight 1 because NEE cannot generate them.
7. Allow BSDF-only / NEE-only / MIS modes. Default remains BSDF-only until the verified
   MIS path is ready. NEE-only suppresses secondary emission only for table emitters.
   Both new techniques are gated by emissiveEnabled. Use the same finite path-segment
   limit: NEE runs only if a BSDF continuation segment would be allowed, avoiding a
   one-vs-two-segment mismatch at the final bounce.
8. Rebuild/refit the table for geometry, instance transform, material or emission changes;
   these changes reset accumulation. Do not reuse an old area/PDF after a transform edit.
   Environment MIS PDFs and analytic-light delta semantics are not modified by this table.

## CPU table follow-up

Base: `49e541c`. `Scene/EmissiveTriangleTable.h/.cpp` adds a pure extraction API
without changing the rendering path. Each record preserves world vertices, UVs,
instance ID, primitive index, resolved material ID, double-precision area, area-weighted
selection PDF and CDF. Matrices are transposed back from InstanceData GPU storage
before transforming vertices. Indexed ranges use the scene's absolute vertex indices;
non-indexed ranges use firstVertex. The material is resolved from the first vertex
or the instance sentinel, matching SceneRayQuery.

Potential emission checks the texture RGB against positive emission-factor channels;
a partially black texture remains eligible. Missing textures use the renderer's black
fallback semantics. glTF factor-only emitters already receive a white texture during
import. Invalid references, malformed RGBA8 data, nonfinite/negative emission parameters,
nonfinite/projective transforms and incomplete triangles are rejected. Zero-area
triangles are skipped. Runtime mirrored or singular instance transforms are explicitly
rejected by this initial table policy; baked glTF mirrors use their corrected geometry
with a positive instance transform and are not rejected on that basis.

The CPU table is not a GPU ABI. GPU serialization, float-CDF precision policy,
resource lifetime, update/reset hooks and shader binding remain for the next batch.
No emitter buffer is uploaded and no NEE/MIS is enabled by this change.

Debug x64 MSBuild succeeded after a local Windows max-macro compatibility repair.
CMake built the emitter test target; CTest passed 25/25, including the new test.
Python remained 70/70. Tests cover shared mesh instances with area ratio 1:6,
nonuniform transform/translation, separate mesh ranges and material remapping,
non-indexed geometry, zero/missing/partly black emission textures, disjoint texture/factor
channels, degenerate geometry and invalid geometry/material/texture/transform inputs.
No new GPU capture is needed for this unconnected CPU-only batch; GPU integration
must be validated separately before claiming runtime behavior.

## GPU format and sampling helpers

Base: `b7cdba3`. `EmissiveTriangleGpu` is an 88-byte structured-buffer record.
Static assertions guard area/CDF/PDF/UV/identity offsets at 12/28/44/48/72 bytes.
`SerializeEmissiveTriangleTable` converts the double-precision CPU distribution
to float, derives selectionPdf from the actual float CDF interval width and fixes
the last CDF endpoint at one. Zero-width float intervals and unrepresentable areas
are rejected, not silently removed or assigned an unrelated ideal probability.
The shader declaration matches the CPU fields without double values or pointers.

`EmissiveTriangleSampling.hlsli` adds upper-bound CDF selection (return=count means
no selection), square-root barycentrics and the one-sided area-to-solid-angle PDF.
Selection input must be in [0,1); random barycentric inputs must be in [0,1).
The geometric normal follows the cross product used by SceneRayQuery. Degenerate,
back-facing or nonfinite PDF configurations return zero. This helper is compiled
through the PT shader include; it is not yet called by TracePath.

Serialization tests cover the 1:6 area distribution, exact float-interval PDFs,
identity/stride, empty tables, collapsed float CDF intervals and overflowing area.
At this format-only stage, GPU upload, per-frame ownership, runtime refresh/reset
and native shader-output validation remained open. See the upload follow-up below.

## GPU upload and binding follow-up

Base: `c59d96c`. Each FrameResource owns an upload buffer and capacity/version.
The current frame slot is reused only after the existing fence wait. The table is
bound as a root SRV at t7/root index 19; the PT root constants grow from 32 to 36
DWORDs with emitter count plus three padding words. The complete root signature
uses 60 DWORDs, below the 64-DWORD limit. Empty/unavailable tables use a valid zeroed
dummy record and count zero. A missing buffer uses the existing unsupported PT clear
instead of dereferencing a null resource.

Static scenes reuse extracted records. Visible instance count, instance world/mesh/material
identity and effective material changes rebuild the table; previous-world matrices alone
do not trigger a rebuild. Extraction applies runtime GPU emission scale/factors rather
than stale SceneMaterial values, and excludes unlit/out-of-range GPU materials.
Geometry/texture/material resource recreation invalidates the cache. Table changes reset
PT accumulation. Arbitrary CPU vertex/texture mutation without the normal renderer
resource-update path is not a supported update contract.

Extraction or float-serialization errors clear the table, preserve BSDF-only rendering
and publish the reason through `emissiveTableStatus`. The capture diagnostics also report
`emissiveTriangleCount`. Allocation/map failures remain ordinary renderer failures; they
are not misreported as successful table extraction. Scene release clears all table caches
and frame buffers. No new RenderGraph redesign or descriptor allocation is introduced.

The bound table is not consumed for lighting yet: the optimizer may remove its unused
shader declaration. These tests verify allocation/binding compatibility and regression,
not shader record decoding or MIS correctness. Actual GPU PDF/sampling validation belongs
to the next NEE batch.

Reproduction of the emitter upload check uses the freshly built Debug executable:

```powershell
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-upload --samples 64 --require-emitter-table
```

The flag requires ready status and exactly two emitter records in each BSDF cohort.
GPU source/output evidence is retained in
`path-tracing-validation-results/completion-step-5-emissive-upload-summary.json`.

Final Debug MSBuild and CMake builds succeeded. CTest passed 25/25 and Python
70/70. The four 64 spp BSDF seeds plus emission-off control passed on RTX 2080 Ti;
all five PFM hashes exactly matched the pre-upload baseline. Emissive count was two
and status ready. A sixth capture with no emitter verified ready/count=0 and the
dummy-buffer path. All six captures had zero D3D12 ERROR/CORRUPTION lines. The
independent area-reference discrepancy remained 0.3278%. CPU tests additionally
check effective emission scale overrides and override-list size rejection.

Interactive transform/material editing and shader-side record decoding are not
established by this static campaign. They remain explicit verification targets for
the upcoming NEE/MIS integration.

## Implementation batches

### NEE-only integration

`emissiveSamplingMode` is persisted in renderer settings: 0 is the unchanged
BSDF-only default and 1 is NEE-only. The PT UI exposes both modes; changing the
mode resets accumulation. Capture diagnostics record the effective setting.
The new value occupies an existing padding word, keeping 36 root constants and
the 60-DWORD root-signature budget unchanged.

NEE selects an uploaded triangle using its float CDF, samples uniform area via
square-root barycentrics and evaluates the same textured emission/UV transform
as BSDF hits. Its solid-angle PDF includes both triangle selection probability
and the area-to-solid-angle Jacobian. Evaluation uses the continuation ray's
normal-offset origin and the existing diffuse/GGX mixture BRDF.

Visibility traces only to the sampled endpoint (one float ULP expansion, capped
by rayTMax), with the same back-face policy as BSDF continuation. A closest hit
on the selected instance/primitive is the light endpoint, not an occluder; a
different closest hit blocks it. No fixed world-unit endpoint subtraction or
blanket exclusion of all triangles belonging to the emitting object is used.

NEE runs only if an additional path segment fits the bounce budget. Secondary
emission from table members is suppressed only when NEE was active at the
previous vertex. Primary-visible emission, unlit surfaces and non-table emitters
remain visible. Empty/unavailable tables and disabled shadows retain BSDF-only
transport rather than suppressing emission without a matching estimator.

The validated NEE-only batch does not establish MIS correctness. Shadow-off/empty-table fallback, nonuniform textures,
many emitters, tiny emitters, back-facing and beyond-endpoint blocker campaigns
remain targets for the next expanded validation batch.

Debug MSBuild and CMake succeeded; CTest passed 25/25 and Python tests 71/71.
On RTX 2080 Ti, the 64 spp / four-seed campaign passed for both estimators:
BSDF mean 0.0555422220 (0.3278% reference difference), NEE mean 0.0556517462
(0.1313% difference), independent reference 0.0557248885. Four-standard-error
relative uncertainty was 1.0606% for BSDF and 0.0657% for NEE. This uncertainty
comparison describes the four ROI-mean observations, not a general image-variance
guarantee. The four BSDF PFM hashes exactly match the upload baseline.

Both blocked-mode ROIs, the one-bounce ROI and emission-off ROI were exactly zero.
Primary-visible emission matched the expected RGB within 4.65e-7 for both modes.
All 14 captures had zero D3D12 ERROR/CORRUPTION messages. Retained evidence is in
`path-tracing-validation-results/completion-step-5-emissive-nee-summary.json`.

```powershell
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-nee --samples 64 --modes 0 1 --require-emitter-table --visibility-controls
```

1. [x] Baseline reference and diagnostics.
2. [x] CPU emitter extraction, stable identity and GPU table plumbing, with tests for
   shared BLAS instances, separate mesh ranges, mirrored baked nodes and unequal areas.
3. [*] NEE-only finite visibility is validated; paired MIS is implemented but GPU validation is pending.
4. [ ] Expanded multi-seed GPU comparisons of BSDF-only/NEE-only/MIS, blocked/off/back-facing controls,
   small/large emitters, multiple unequal emitters and texture modulation. Compare
   means before accepting variance reduction. Keep environment and analytic lights off
   for isolated tests, then run a combined-lighting regression.

## Reproduction

### MIS integration checkpoint

Mode 2 is exposed as `MIS (BSDF + NEE)` in both the normal PT UI and Scene Editor.
NEE samples use the power heuristic with the full triangle-selection/solid-angle
PDF and the existing mixture BSDF PDF. BSDF hits identify the corresponding table
record and evaluate the complementary weight from the continuation ray origin
and hit position. Primary-visible/non-table emission and fallback policies remain
unchanged. The comparison runner accepts modes 0/1/2 and reports paired seed-mean
agreement in addition to the independent reference comparison.

Scene Editor now exposes Path Tracing, accumulation, samples/frame, bounce budget,
lighting toggles, emissive sampling, output selection, shadows and reset. Changes
mark the render preset modified; persistence still requires explicit Save Preset.

The MIS shader and engine were Debug-built successfully before the final UI-label
edits. The final UI edits passed C++ compilation and Python tests passed 71/71.
MIS multi-seed native captures and updated CTest have not yet been run at this
checkpoint. Do not treat the earlier NEE evidence as MIS validation.

```powershell
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-repeat --samples 64
```
