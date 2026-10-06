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
- [x] Invoke GPU sampling/PDF lookup from TracePath.
- [x] Add area-sampled NEE with finite endpoint visibility.
- [x] Add matching BSDF-hit MIS and explicit sampling modes.
- [x] Validate mean agreement, occlusion, multiple emitters, texture and size controls.
- [x] Validate deep-path RR mean preservation.
- [x] Validate combined-lighting additivity and joint MIS.
- [x] Complete final convergence checks.

Step 5 is complete within the declared validation domains as of 2026-10-06.
Emissive BSDF-only, NEE-only and MIS shader paths are
implemented and the native campaigns below cover their declared fixture domains.
Primary-visible emission retains unit weight. Environment NEE/MIS and analytic
scene lights remain separate techniques; their additivity is validated below.

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
3. [x] NEE-only finite visibility and paired MIS are validated in the basic constant-emitter campaign.
4. [*] Expanded multi-seed GPU comparisons of BSDF-only/NEE-only/MIS, blocked/off/back-facing controls,
   small/large emitters, multiple unequal emitters and texture modulation. Compare
   means before accepting variance reduction. Keep environment and analytic lights off
   for isolated tests, then run a combined-lighting regression.

## Reproduction

### Unequal and textured emitters (2026-10-05)

Two additional fixture families passed 64 spp/four-seed comparisons across all
three modes, including RGB-channel reference agreement and paired-seed mean checks.
The unequal family uses 2x2 and 1x1 panels at y=3, centered at x=-2/+2 with warm/cool
emission colors. Its four GPU triangles test area selection ratio 4:1, separate
material/range identity and positive nonuniform instance scale.

The texture family uses a 3x3 panel with explicit 0-1 UVs and an embedded black/white
2x2 PNG. The independent integral applies bilinear WRAP filtering in sRGB space,
then sRGB-to-linear decoding and emission factors. Quadrature intervals are split
at filter boundaries. Order 64/128 relative change was 1.21e-10.

Independent-reference discrepancies (BSDF/NEE/MIS) were 0.111%/0.160%/0.162% for
unequal emitters and 0.246%/0.048%/0.057% for the texture family. Both MIS emission-off
controls had exact zero ROI maxima. Twenty-six captures completed with no D3D12
ERROR/CORRUPTION lines. GPU tables were ready with four/two triangles respectively.
No renderer correction was required. Python tests passed 76/76; CTest passed 25/25.

Numbered scenes 03 and 04 now retain the inputs, Japanese Description and evaluation
results under Assets/Scene/PathTracingValidation. Complete campaign provenance is
in `path-tracing-validation-results/completion-step-5-emissive-extended-summary.json`.
The original raw report's constant-emitter limitation label is corrected in the
retained summary; only metadata wording changed after capture, not the integrand.

Tiny/large emitters, back-facing and beyond-endpoint blockers, empty-table/shadow-off
fallback, deeper RR paths and combined lighting remain pending before Step 5 closure.

```powershell
python -B Tests/PathTracing/validate_emissive_extended.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-extended-repeat --samples 64
```

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

The initial integration checkpoint did not claim GPU MIS validation. The following
2026-10-05 campaign adds separate evidence; the older NEE report remains unchanged.

### Basic MIS validation (2026-10-05)

The current CMake Debug application was used via the runner's optional `--exe`
argument. Twenty native captures passed: three modes x four seeds at 64 spp,
three blocked controls, three primary-visible controls, one MIS one-bounce control
and one MIS emission-off control. The independent quadrature reference was
0.0557248885. ROI means were BSDF 0.0555422220 (0.3278% discrepancy), NEE
0.0556517443 (0.1313%) and MIS 0.0556538089 (0.1276%). All independent-reference
and paired-seed agreement checks passed. MIS vs BSDF mean difference was 0.2009%
with four-standard-error relative uncertainty 1.0554%.

Blocked/off/one-bounce ROI maxima were exactly zero. Primary-visible emission
matched the expected RGB within 4.65e-7 for all three modes. All images were
1920x1080, emitter count was two with ready status, and the log scan found zero
D3D12 ERROR/CORRUPTION lines. CTest passed 25/25 and Python tests 72/72.

The four-seed ROI-mean uncertainty is not a general image-variance guarantee.
Multiple unequal emitters, texture modulation, tiny/large emitters, back-facing
and beyond-endpoint blockers, deeper RR paths and combined lighting remain pending.
Step 5 is therefore not complete yet.

### Visibility and fallback controls (2026-10-06)

Fifteen CMake Debug captures passed at 64 spp with seed 11, covering baseline,
back-facing emitter, beyond-endpoint blocker, empty emitter table and shadows OFF,
each in BSDF/NEE/MIS modes. Back-facing receiver ROI values were exactly zero.
Beyond-endpoint blockers produced identical full HDR file hashes to the baseline
for each mode. Empty-table and shadows-OFF captures produced identical full HDR
hashes across all three modes, with nonzero receiver illumination. Empty-table
validation uses constant-white environment sampling mode 1; other cases disable
environment and direct lighting. The empty table reported count zero and ready.

No ERROR/CORRUPTION messages were found. The 2-4 warnings per capture are existing
buffer initial-state warnings. Python tests passed 81/81 and CTest passed 25/25.
Numbered assets 05-08 retain Japanese descriptions and evaluation hashes.
The archived relative preset path is render-preset.json, not the runner's preset.json.
This fixed-seed equality campaign does not replace multi-seed convergence checks.
Tiny/large emitters, deeper Russian Roulette paths and combined lighting remain
pending; Step 5 is not yet complete.

Evidence: path-tracing-validation-results/completion-step-5-emissive-controls-summary.json.

```powershell
python -B Tests/PathTracing/validate_emissive_controls.py --output bin/PathTracingValidation/emissive-controls-repeat --samples 64 --seed 11
```

Provenance and capture hashes are retained in
`path-tracing-validation-results/completion-step-5-emissive-mis-summary.json`.
The numbered baseline asset also retains `evaluation-mis-results.json` alongside
the original NEE-only result. Its Description now records the basic MIS checks.

```powershell
python -B Tests/PathTracing/validate_emissive.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-mis-repeat --samples 64 --modes 0 1 2 --require-emitter-table --visibility-controls
```

```powershell
python -B -m unittest discover -s Tests/PathTracing -p 'test_*.py'
python -B Tests/PathTracing/validate_emissive.py --output bin/PathTracingValidation/emissive-repeat --samples 64
```

### Small and large emitters (2026-10-06)

Twenty-six CMake Debug captures passed: two square emitter sizes, three modes,
four seeds (11/23/37/53), 256 spp, plus one emission-OFF capture for each size.
The small emitter is 0.3m square and the large emitter is 9m square; both are at
height 3m. Transform scale changes X/Z only, preserving height and orientation.

| Emitter | Reference mean | BSDF discrepancy | NEE discrepancy | MIS discrepancy |
| --- | --- | --- | --- | --- |
| 0.3m square | 0.0007314670 | 0.9040% | 0.2219% | 0.2220% |
| 9m square | 0.1734776053 | 0.0681% | 0.0540% | 0.0287% |

All RGB-channel reference comparisons and paired BSDF/NEE/MIS checks passed.
Small-emitter BSDF four-standard-error relative uncertainty was 2.2146%, below
the unchanged 5% inconclusive threshold. NEE/MIS uncertainty was approximately
0.0019%. These are seed-mean uncertainties, not per-pixel variance guarantees.
64/128-point quadrature changes were zero for the small emitter and 1.12e-14 for
the large one. Both emission-OFF receiver ROI maxima were exactly zero.

Captures reported two ready emitter triangles and 1920x1080 dimensions. Logs
contained no ERROR/CORRUPTION messages. CTest passed 25/25; Python passed 83/83.
Renderer/shader code was unchanged; the already-built current CMake Debug binary
was used. Assets 09/10 retain geometry, presets, Japanese descriptions and results.
Evidence: path-tracing-validation-results/completion-step-5-emissive-size-summary.json.

The tested domain remains two-segment opaque one-sided transport. Deeper Russian
Roulette paths and combined lighting are still pending; Step 5 is not complete.

```powershell
python -B Tests/PathTracing/validate_emissive_extended.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-size-repeat --samples 256 --cases small large
```

### Deep emissive transport and RR (2026-10-06)

An enclosed diffuse room illuminated only by one emissive panel was captured at
128 spp with seeds 11/23/37/53. Twenty-nine captures passed: BSDF/NEE/MIS x RR
OFF/ON at eight path segments (24), MIS/RR OFF at two segments (4), and a final
MIS/RR ON emission-OFF control (1). Diagnostics confirm two ready emitter triangles,
the requested RR/mode/bounce settings and 1920x1080 dimensions. RR changes the
output hashes for all three modes, so this is not a no-op toggle comparison.

| Mode | RR OFF mean | RR ON mean | Difference | Four-SE relative uncertainty |
| --- | --- | --- | --- | --- |
| BSDF | 0.0303966622 | 0.0303597972 | 0.1213% | 0.5645% |
| NEE | 0.0304008159 | 0.0303796632 | 0.0696% | 0.2582% |
| MIS | 0.0304004299 | 0.0303799640 | 0.0673% | 0.2542% |

All seven paired comparisons passed under the unchanged 2% tolerance plus four-SE
uncertainty, with the existing 5% inconclusive threshold. BSDF vs NEE/MIS differences
were below 0.067% for both RR states. MIS two-segment mean was 0.0122304060 versus
eight-segment mean 0.0304004299. The additional 0.0181700238 contribution exceeds
its four-SE absolute uncertainty 0.0000298179 plus the 0.0001 signal floor.
The emission-OFF receiver ROI maximum was exactly zero.

No ERROR/CORRUPTION messages were found; existing buffer initial-state warnings
remain. Python passed 87/87 and CTest 25/25. Renderer/shader code was unchanged.
The initial attempt stopped because the runner used the wrong RR diagnostic key;
the corrected campaign is separate and the failed raw attempt is preserved locally.

Asset 11 retains the scene, preset, Japanese Description and report. Evidence:
path-tracing-validation-results/completion-step-5-emissive-rr-summary.json.
This validates relative mean preservation in one enclosed diffuse room, not an
independent absolute deep-transport oracle or all material/RR families. Combined
emissive/analytic/environment lighting remains pending; Step 5 is not complete yet.

```powershell
python -B Tests/PathTracing/validate_emissive_rr.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-rr-repeat --samples 128
```

### Combined lighting (2026-10-06)

Thirty-seven CMake Debug captures passed at 128 spp and seeds 11/23/37/53.
The receiver/emitter geometry is unchanged between lighting cohorts. Sources are
warm emission RGB (0.8,0.4,0.2), a cool point light RGB (0.2,0.5,1), intensity 4
at (-2,2,-1), and constant-white environment intensity 0.2. Shadows are ON,
max path segments are two, and RR is OFF. Environment mode 5 uses constant MIS;
the independent technique comparison uses emissive BSDF and environment mode 1.

Captured cohorts: emissive-only modes 0/1/2 (12), direct-only (4), environment-only
(4), combined modes 0/1/2 (12), combined pure-BSDF environment (4), and all-OFF (1).
Each seed's isolated-source RGB sum is compared with its combined capture. All
six comparisons x mean/R/G/B (24 checks) passed under unchanged tolerances.

| Emissive mode | Isolated sum mean | Combined mean | Discrepancy |
| --- | --- | --- | --- |
| BSDF | 0.1604140364 | 0.1605481058 | 0.0836% |
| NEE | 0.1605173262 | 0.1605666975 | 0.0308% |
| MIS | 0.1605139698 | 0.1605606874 | 0.0291% |

Maximum RGB-channel additivity discrepancy was 0.1258%. Joint MIS vs pure BSDF
mean discrepancy was 0.0561%, with maximum RGB discrepancy 0.0874%. The largest
four-SE relative uncertainty across all checks was 0.2804%, below the unchanged
5% inconclusive threshold. The all-OFF receiver ROI maximum was exactly zero.
All three isolated sources exceeded the 0.0001 signal floor.

No ERROR/CORRUPTION messages were found; existing buffer initial-state warnings
remain. Python passed 91/91 and CTest 25/25. No renderer/shader edit was required.
Asset 12 retains geometry, preset, Japanese Description and evaluation results.
Evidence: path-tracing-validation-results/completion-step-5-emissive-combined-summary.json.

This confirms additivity and technique agreement in the declared fixture, not an
absolute transport oracle or arbitrary imported-scene lighting. The final remaining
Step 5 checkpoint is an explicit sample-count convergence campaign before Step 6
accumulation/reset validation.

```powershell
python -B Tests/PathTracing/validate_emissive_combined.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-combined-repeat --samples 128
```

### Sample-count convergence (2026-10-06)

Thirty-seven CMake Debug captures passed: BSDF/NEE/MIS x 16/64/256 spp x four
seeds (11/23/37/53), plus one exact repeat of MIS/256 spp/seed 11. The baseline
fixture is unchanged: two segments, only emission, shadows ON and RR OFF.
All nine cohort means and RGB channels passed the independent area-reference
comparison; all six paired BSDF/NEE/MIS mean comparisons passed.

Noise is mean unbiased per-pixel RGB variance across seeds (ddof=1), not variance
across pixels. Static shading does not count as noise. Predeclared requirements:
each >=4x sample increase has variance ratio <=0.65, final/initial ratio <=0.25,
and log-variance slope <=-0.5. Zero variance is rejected as a missing stochastic
signal. These are fixture diagnostics, not formal confidence bounds or proof
of zero deterministic bias.

| Mode | Variance at 16 spp | At 64 spp | At 256 spp | Final/initial | Log slope |
| --- | --- | --- | --- | --- | --- |
| BSDF | 9.06111e-4 | 2.24844e-4 | 5.70161e-5 | 0.062924 | -0.99756 |
| NEE | 9.26401e-6 | 2.36031e-6 | 5.77786e-7 | 0.062369 | -1.00076 |
| MIS | 9.43877e-6 | 2.40517e-6 | 5.97660e-7 | 0.063320 | -0.99530 |

All curves show approximately inverse-sample-count variance. This is not
cost-normalized: NEE/MIS perform extra work per primary sample. The repeat PFM
hash matched exactly. No ERROR/CORRUPTION messages were found. Python passed
96/96 and CTest 25/25; renderer/shader code was unchanged.

Baseline asset 01 retains evaluation-convergence-results.json. Evidence:
path-tracing-validation-results/completion-step-5-emissive-convergence-summary.json.
Step 5 is complete for the measured opaque one-sided affine emitter/receiver
fixtures, including visibility, texture, size, RR and joint-lighting controls.
Arbitrary imported/unlit/transparent emitters, all RR material families, per-pixel
bias guarantees and cross-GPU replication are not claimed. Step 6 follows with
accumulation count, pause/reset and invalidation validation.

```powershell
python -B Tests/PathTracing/validate_emissive_convergence.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/emissive-convergence-repeat --samples 16 64 256
```
