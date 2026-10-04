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
- [ ] Serialize/upload the table and add GPU sampling/PDF lookup.
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

## Implementation batches

1. Baseline reference and diagnostics (current batch).
2. CPU emitter extraction, stable identity and GPU table plumbing, with tests for
   shared BLAS instances, separate mesh ranges, mirrored baked nodes and unequal areas.
3. NEE-only finite visibility, then paired MIS with BSDF-hit PDF evaluation.
4. Multi-seed GPU comparisons of BSDF-only/NEE-only/MIS, blocked/off/back-facing controls,
   small/large emitters, multiple unequal emitters and texture modulation. Compare
   means before accepting variance reduction. Keep environment and analytic lights off
   for isolated tests, then run a combined-lighting regression.

## Reproduction

```powershell
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-repeat --samples 64
```
