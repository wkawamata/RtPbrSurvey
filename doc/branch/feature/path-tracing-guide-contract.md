# Path tracing guide contract: Step 8

Date: 2026-10-10. Workspace: C:/work/RtPbrSurvey-work-3.
Branch: codex/pt-correctness-completion.

## Scope

Audit native PT outputs and validate signal consistency before any NRD/DLSS RR
connection. Native accumulation remains the reference path. No denoiser SDK,
backend call, shader estimator change or packing convention is introduced here.

All six guides already have RenderGraph resources and Debug Texture Preview
registration. Native .ptbuf capture previously selected only the first four;
the selection loop now covers PathTracingGuideTextureCount, including both
radiance signals. The existing asynchronous readback/fence and state restoration
remain unchanged. Accumulation is also available as a separate native capture.

## Native outputs

All resources are render-resolution, top-down captures. The guides are written
each frame, independently of progressive radiance history. World coordinates
follow the engine's left-handed convention. Native capture does not apply UI
display mappings, exposure, tone mapping or gamma conversion.

| Resource (PathTracing prefix) | DXGI format | Native meaning |
| --- | --- | --- |
| NormalRoughness | RGBA16_FLOAT | World shading normal xyz; effective material roughness w |
| ViewZ | R32_FLOAT | Positive camera-view-axis primary depth, world units; not device depth or Euclidean distance |
| MotionVectors | RG16_FLOAT | Previous NDC xy minus current NDC xy for the same primary surface, using previous object transform |
| Albedo | RGBA16_FLOAT | Linear primary base color RGB; alpha 1 for hit, 0 for miss |
| DiffuseRadianceHitT | RGBA16_FLOAT | Noisy current-frame diffuse-classified radiance RGB; primary ray hitT alpha |
| SpecularRadianceHitT | RGBA16_FLOAT | Noisy current-frame specular-classified radiance RGB; same primary ray hitT alpha |
| Accumulation | RGBA32_FLOAT | Radiance sum RGB; accumulated sample count alpha |

Primary guides and hitT are from the first sample in a frame. Radiance signals
are averaged over that frame's sample batch. With multiple samples at a boundary,
the first primary hit need not describe every contributing sample. Motion is not
pixels or UV: previous-minus-current pixels = NDC * (width/2, -height/2).
PT disables temporal-upscaler projection jitter but retains stochastic primary
ray sample positions. Motion quality for animated/skinned geometry is not implied.

First-sample miss sentinels: NormalRoughness=(0,0,0,1); ViewZ=0;
MotionVectors=(0,0); Albedo=(0,0,0,0); both hitT alpha values=0.
Radiance RGB is zero when all samples miss, not necessarily when only the first
sample misses: later surface samples still contribute to the batch-average RGB.
Primary environment
background is included in total radiance but not in either separated surface
signal. It must be composited explicitly rather than denoised as a surface hit.

The signal classification follows the first scattering lobe; primary emission
and unlit terms are currently placed in the diffuse class. Base color is not
already diffuse albedo or specular F0, and the two signals are not demodulated.
The hitT value is raw primary ray distance from the traced origin, not normalized
NRD hit distance, not secondary diffuse/specular travel distance and not ViewZ.
Do not connect these alpha channels directly to a backend by matching names.

## Numeric checks

validate_guide_contract.py makes a deterministic roughness fixture with emission
disabled, one sample/frame and accumulation disabled. It captures all six guides
plus Accumulation after 30 frames, preserving resource/format/dimensions, seed,
sample index and matrices. All captures must have the same frame metadata.

Checks cover finite payloads, required formats, both hit/miss populations, unit
hit normals within .002, roughness in [0,1], positive hit ViewZ/hitT, nonnegative
color/signal values, exact miss sentinels, equal diffuse/specular hitT and sample
count 1. On hit pixels, diffuse+specular must match current radiance within
0.002 * max(abs(radiance),1) + 1e-5, allowing half-storage rounding. Background
is deliberately excluded from that identity because primary miss radiance is not
partitioned. This is an internal consistency test, not independent BSDF accuracy.

```powershell
python -B Tests/PathTracing/validate_guide_contract.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step8-guide-contract
python -B Tests/PathTracing/validate_inputs.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step8-primary-guides
```

Use a new output directory and a fresh Debug executable. validate_inputs.py now
accepts --exe and defaults to the current CMake build rather than stale bin output.
Its existing independent plane/motion tests remain separate from the signal
partition checks. Raw PTBUF/log/scene/preset files stay under ignored bin output.

## Backend Readiness

Native guide availability is not equivalent to NRD or DLSS RR readiness.
Before connecting a backend:

- Select an exact backend/version and document its input encoding and units.
- Define diffuse/specular demodulation and the required material/F0 guides.
- Define backend-specific hit distance semantics/packing instead of raw primary T.
- Handle background, emission, unlit surfaces and multisample guide consistency.
- Validate object motion, normal maps, nonuniform scale and spatial textures.
- Specify render/output sizes, jitter, exposure, history reset and disocclusion.
- Preserve native A/B capture and isolate all SDK types within the adapter.

The current native contract is suitable for inspection and further validation,
not advertised as a completed denoiser integration. The backend conversion belongs
at the integration boundary, not in scene data or broad public SDK-dependent APIs.

## Results

Debug x64 MSBuild: success, zero warnings/errors. Python tests: 120/120 passed.
Executable SHA-256:
a49186c9154b24feb4a7d81f89cc5ef592d9fc798e6b366ec5ee5da219df7940.

Seven native captures passed all fourteen contract checks, with 85,892 hit pixels
and 1,987,708 miss pixels. Maximum diffuse+specular versus frame-radiance absolute
error was 0.001922607421875, within the declared relative/half-storage bound.
The actual captured sampleStartIndex was 30, identical across all seven captures.
No D3D12 error/corruption entries or process failures occurred.

Four independent primary-guide regression captures also passed:

| Check | Maximum error / result |
| --- | --- |
| Plane normal/roughness | 0.00048828125, within .001 |
| Shifted camera-axis ViewZ | 0.00000476837, within .0001 |
| Marker Albedo | 0.0004450518, within .0015; zero material mismatches |
| Moving shifted-camera MV | Max bound-normalized error 0.53871, below 1 |

MV still exceeds the historical fixed 2e-5 NDC threshold: maximum absolute error
0.00003070534. Its accepted policy remains the predeclared format-derived
2 half ULP + 3e-6 bound, not a new relaxed threshold chosen for this campaign.

Raw evidence: bin/PathTracingValidation/completion-step8-guide-contract-20261010
and completion-step8-primary-regression-20261010. The first contract report was
generated before adding source/fixture provenance fields to the runner; its
executable and per-capture hashes are retained, but those new fields must not be
claimed as part of that historical report. Future runs include them.
Committed-summary destination: path-tracing-validation-results/
completion-step-8-guide-contract-summary.json (not yet committed).

This completes the initial contract audit and static signal-consistency phase.
Backend-specific signal/guide conversion and the coverage gaps above remain
separate work; no denoiser readiness claim is made.

## Multisample Boundary Validation

The marker fixture uses two materials on two front-facing planes, maxBounces=2,
emission disabled, one frame of accumulation and 1/2/4 samples per frame. Seven
resources were captured for each batch: 21 captures. Capture delays 60/30/15
align the primary sampleStartIndex at 60 for every batch, with seed 7 and identical
static camera matrices. All four primary guides are bit-identical across batches.
Both diffuse and specular signals must be nonzero; all-zero signals are rejected
even if their partition identity trivially matches zero radiance.

Each batch passes its sixteen checks, including count-alpha equal to its batch
size and diffuse+specular equal to Accumulation RGB divided by that count:

| Batch | Partition max error | Mixed-material pixels in dense interior ROI |
| --- | ---: | ---: |
| 1 | 0.001026034355 | 0 |
| 2 | 0.000993728638 | 643 |
| 4 | 0.000985980034 | 1126 |

The independent plane-hit model predicts material IDs for each stochastic primary
sample, not only the first. The ROI covers 1,978,624 pixels, excludes a 16-pixel
outer border and uses stride 1. A pixel is mixed if any sample's material differs
from its first sample. This all-hit fixture does not validate mixed hit/miss
background handling. Sentinel checks on empty miss populations are not evidence
for that case; the separate roughness fixture provides single-sample miss coverage.

The first attempt used a one-segment marker preset, producing zero surface
signals. It was interrupted and excluded as insufficient evidence, not treated
as a useful signal-partition pass. The corrected two-segment cohort initially
used stride-4 analysis and failed to find mixed-material pixels. This sparse
analysis is preserved as report-before-dense-analysis.json; dense reanalysis of
the same captures passes without relaxing the guide-equality or signal bounds.
No captures were selectively removed. Python tests now pass 123/123.

Raw corrected cohort: bin/PathTracingValidation/
completion-step8-batch-guides-20261010-r2. Earlier excluded cohort:
completion-step8-batch-guides-20261010. The executable is unchanged from the
initial Step 8 tests. No shader, material or renderer signal changes were made.

```powershell
python -B Tests/PathTracing/validate_guide_contract.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step8-batch-guides --batch-matrix
python -B Tests/PathTracing/validate_guide_contract.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step8-batch-guides --analyze-batch-matrix
```

Decision: retain the Native multisample semantics; do not average normals,
roughness or primary hitT blindly. An initial denoiser experiment should use one
sample per frame and explicit backend conversion. Supporting larger batches
requires a separately validated guide/signal association and boundary policy.
This is an integration requirement, not evidence of a defect in Native accumulation.

## Object Motion Validation

An opt-in `-PathTracingObjectMotionX <delta>` translates the single instance of a
file scene once per frame through the existing SetScene/TLAS rebuild path. The
delta is a finite world-space X displacement in [-1, 1]. SceneFile and CapturePath
are required. Without the flag, application behavior is unchanged. Native capture
metadata now records current/previous object matrices for single-instance scenes;
multi-instance captures use null for those fields.

The input-plane fixture retains its original orientation. Independent analysis
uses primary ray-plane hits on z=0 and computes previous world hits by subtracting
the requested X displacement, then projects with the recorded camera matrices.
It checks unchanged camera matrices and that the full object-matrix difference
contains only the requested translation. Shader motion-vector code was not changed.

Fresh Debug x64 captures on RTX 2080 Ti, driver 616.56, seed 7, 30 warm-up frames:

| Object delta/frame | Pixels tested | Max absolute NDC error | Max bound-normalized error |
| --- | ---: | ---: | ---: |
| 0 | 123664 | 0.000001847744 | 0.592376 |
| +0.05 | 123664 | 0.000002301931 | 0.611485 |
| -0.05 | 123664 | 0.000002301931 | 0.592376 |

All three pass both the historical 2e-5 absolute diagnostic and the predeclared
2 half ULP + 3e-6 NDC acceptance bound. Observed mean X vectors are approximately
0, -0.0108032 and +0.0108032 respectively, confirming previous-minus-current
sign. All logs contain zero D3D12 ERROR/CORRUPTION entries. Debug build reports
zero warnings/errors and Python unit tests pass 130/130.

Raw evidence: bin/PathTracingValidation/completion-step8-object-motion-20261010-r2.
Executable SHA256: 65754f1fcd88aea1ae01a6964c355b17b3c866b91d201f5346983d4b0e8862da.
The report records HEAD 3885c98 plus dirty source hashes: the motion diagnostic
was not committed when tested. The earlier cohort failed an overly strict test
assumption that the plane's original matrix was identity; its report is retained
as report-before-transform-check.json in completion-step8-object-motion-20261010.
Correcting the fixture assumption required no shader change or tolerance change.

```powershell
python -B Tests/PathTracing/validate_object_motion.py --output bin/PathTracingValidation/step8-object-motion
```

Coverage remains limited to a rigid single plane translated along world X with
a fixed camera and single-sample primary guides. Rotating/scaling/deforming
objects, multiple visible instances, disocclusion boundaries, normal maps and
spatial material textures are not validated by this cohort. This is not a
backend-specific denoiser-readiness claim.

## Spatial Texture And Normal Map Validation

The input-textured fixture is a self-contained opaque glTF plane with embedded
geometry and three lossless 16x16 RGBA PNG textures. Four constant regions have
different base-color, tangent-normal and roughness bytes. Base-color factor is
(0.8, 0.6, 0.4); roughness factor is 0.7. Tangents are explicitly supplied.
The fixture generator is make_textured_fixture.py; regeneration is deterministic
and unit-tested against the checked-in JSON assets.

Three conditions capture native NormalRoughness and Albedo, for six captures:
normal scale 0.5, normal scale 0, and glTF-baked X reflection at normal scale 0.5.
The scene-level instance scale remains positive in all conditions. CPU analysis
predicts UV from stochastic primary plane hits and checks all four texture
regions. It uses standard sRGB decode followed by the base-color factor, linear
roughness texels followed by their factor, XY-only normal strength, and an explicit
LH tangent frame including the baked mirror's handedness.

Each capture tests 45,454 pixels across four regions, with at least 11,286 samples
per region. A 1/16 UV margin excludes bilinear transition bands and texture edges.
Tolerance was declared before capture: 0.0015 for RGBA16 Albedo and 0.002 for
RGBA16 NormalRoughness, including texture sampling and half-storage error.

| Condition | NormalRoughness max error | Albedo max error |
| --- | ---: | ---: |
| Normal scale 0.5 | 0.00034204993 | 0.00012176882 |
| Normal scale 0 | 0.00048828125 | 0.00012176882 |
| glTF-baked X reflection | 0.00034204993 | 0.00012176882 |

All six captures pass. Zero strength restores the geometric normal. Baked mirror
flips the expected world-normal X component and texture location; Y/Z and material
semantics remain consistent. Spatial variation is required, preventing a uniform
fallback texture from passing. D3D12 ERROR/CORRUPTION count is zero; Python passes
136/136 tests. No C++ or shader changes were necessary. The previously successful
Debug binary was reused (SHA256 65754f1fcd88aea1ae01a6964c355b17b3c866b91d201f5346983d4b0e8862da).
The tested HEAD is 26ba6fd with new fixture/test files uncommitted at capture time.

Raw evidence: bin/PathTracingValidation/completion-step8-textured-guides-20261010-r2.
The earlier folder without -r2 retains an incomplete campaign: its first mirror
attempt used negative SceneDocument scale, rejected by the existing positive-scale
input contract in SceneDocumentJson.cpp. That invalid-input application displayed
a Debug runtime Abort dialog. One test-owned process was terminated, and the next
invalid-input process exited with code 3. Those failures are not passing renderer
evidence. The corrected campaign uses supported baked glTF reflection; it does not
claim support for negative runtime instance scale. The Part 1 primitive-only unit
test now reads its explicit validation-plan scene list; the new glTF references
are separately tested.

```powershell
python -B Tests/PathTracing/make_textured_fixture.py
python -B Tests/PathTracing/validate_textured_guides.py --output bin/PathTracingValidation/step8-textured-guides
```

Coverage is limited to single-sample primary guides on opaque, front-facing,
supplied-tangent surfaces. Bilinear transition correctness, missing tangents,
alternate UV sets, alpha/double-sided semantics, deformation, texture filtering
at oblique angles and denoised radiance remain outside this cohort. Earlier
material/geometry validation has additional constant-texture transform coverage;
this campaign adds spatial color/roughness/normal variation, not a replacement
for those prior results. Mixed hit/miss multisample guide policy and explicit
backend signal conversion remain follow-up work.

## Multisample Hit/Miss Boundary Validation

input-boundary is a finite 2x2 opaque plane at z=0, with camera z=-5, known
material values, emission/direct lighting disabled, two ray segments and constant
environment BSDF sampling. Accumulation is disabled. The primary miss background
is explicitly set to (0.125, 0.25, 0.5), with skybox disabled. It differs from the
surface environment contribution and is not included in separated surface signals.

For seed 7, batches 1/2/4 use 60/30/15 warm-up frames so their captured first
sampleStartIndex is 60. Seven native resources per batch produce 21 captures.
All four primary guide buffers are bit-identical across batches. CPU analysis
projects the finite plane's silhouette and samples every pixel in a four-pixel
edge band plus a stride-16 full-view control grid: 21,053 distinct pixels/batch.
The first sample contains 7,132 hits and 13,921 misses in that analysis population.

Every sample independently predicts hit/miss against the finite plane. The
comparison is frame radiance minus the fraction of primary misses times the known
background, versus Diffuse RGB plus Specular RGB. First-sample hitT and validity
remain independent of later samples. All-miss controls require zero surface
signals and exact background radiance; both diffuse and specular must be nonzero
on surface controls. Mixed pixels where sample zero misses must retain surface
contributions from later samples.

| Batch | Mixed pixels (nominal CPU classification) | First miss / later hit | First hit / later miss | Accepted partition max error |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0 | 0 | 0 | 0.00048822165 |
| 2 | 776 | 379 | 397 | 0.00048816204 |
| 4 | 1438 | 746 | 692 | 0.00060546398 |

The original strict double-precision classification passes batches 1/2 but fails
one pixel in batch 4: (814,747), sample offset 2, CPU world y=-1.0000000574074366.
The residual background is consistent with two misses while the strict model
predicts three. Its strict max radiance-partition error is 0.12475979328. This
failure is retained, not called a strict pass. The original report is preserved
as report-before-final-analysis.json, and final records retain strict pass/error
fields alongside the accepted interval comparison.

After that pilot, the reference explicitly models hit-classification uncertainty
within 8 * float32 epsilon * max(1, abs(world coordinate)) of either geometry edge,
approximately 9.54e-7 world units at this plane. This is a conservative numerical
reference allowance, not a formal DXR accuracy guarantee. For those samples only,
both hit and miss are allowed, producing an interval for background contribution.
Every pixel remains in the test; no captures are removed. The radiance tolerance
stays 0.002 * max(abs(frame radiance),1) + 1e-5. Batches 1/2 each contain one
ambiguous sample/pixel; batch 4 has two. The first-sample guide comparisons still
pass their original exact hit/miss validity and numeric guide checks. The interval
policy was introduced after observing the strict failure and must not be described
as predeclared for that first cohort.

All batches pass under the explicitly qualified interval policy, and all 21 logs
contain zero D3D12 ERROR/CORRUPTION entries. No C++/shader change or rebuild was
required; the prior Debug executable SHA256 remains
65754f1fcd88aea1ae01a6964c355b17b3c866b91d201f5346983d4b0e8862da.
Capture HEAD is 26ba6fd plus uncommitted test/fixture files. Original capture source
hashes and final analysis hash are stored separately. Python tests pass 144/144.

Raw evidence: bin/PathTracingValidation/completion-step8-hit-miss-20261010.

The interval policy was then frozen and tested prospectively with seed 11, batch
4, seven fresh captures in completion-step8-hit-miss-seed11-20261010. All checks
pass: 21,053 pixels, 1,441 nominal mixed pixels, 791 first-miss/later-hit and 650
first-hit/later-miss. Three samples/pixels are numerically ambiguous. The accepted
max error is 0.00059741735; the separately retained strict error is 0.12500506639
and its strict comparison fails. This additional campaign validates the declared
interval policy on new random sample locations; it does not claim strict DXR/CPU
edge equivalence or cross-batch bit identity for seed 11 (only batch 4 was captured).
All seven follow-up logs are free of ERROR/CORRUPTION entries.

```powershell
python -B Tests/PathTracing/make_boundary_fixture.py
python -B Tests/PathTracing/validate_hit_miss_guides.py --output bin/PathTracingValidation/step8-hit-miss
python -B Tests/PathTracing/validate_hit_miss_guides.py --output bin/PathTracingValidation/step8-hit-miss --analyze-only
python -B Tests/PathTracing/validate_hit_miss_guides.py --output bin/PathTracingValidation/step8-hit-miss-seed11 --batches 4 --seed 11
```

Decision: keep the existing native first-sample guide / all-sample signal semantics.
Do not erase a mixed pixel's surface signals merely because its first-sample
validity is zero. A denoiser adapter still needs an explicit coverage/background
policy or a validated one-sample-per-frame mode. This cohort does not establish
backend packing/demodulation correctness, estimator energy accuracy, arbitrary
mesh silhouettes, alpha coverage or temporal disocclusion quality.
