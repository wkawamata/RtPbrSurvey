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

Miss sentinels: NormalRoughness=(0,0,0,1); ViewZ=0; MotionVectors=(0,0);
Albedo=(0,0,0,0); both radiance/hitT guides=(0,0,0,0). Primary environment
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
