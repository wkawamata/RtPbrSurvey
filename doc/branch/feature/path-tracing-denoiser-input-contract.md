# Path Tracer Denoiser Input Contract And Delivery Plan

Date: 2026-10-10. Base: main 0c175e3 (PR 96).
Branch: codex/pt-denoiser-input-contract.
Status: NRD RELAX selected; SDK-free adapter and opt-in input experiments added.
No denoiser dispatch; Native sampling defaults remain unchanged.

## Current Acceptance State

- [x] Work-3 synchronized to merged main.
- [x] Native PT correctness evidence and qualified limitations preserved.
- [ ] User-assisted integrated GUI review (Step 2): checks 1-2 confirmed; 3-5 pending.
- [x] Initial backend input gap analysis and staged plan (this document).

The pending GUI review is not a passing test. Documentation and input experiments
can proceed, but enabling a backend by default requires the review and the later
numeric/visual gates. Do not attempt to unlock Windows or change driver settings.

## Recommendation

Prototype NRD RELAX diffuse/specular at native resolution first, default-off.
Its separated-signal path matches the inspection goal and lets material weighting,
signal reconstruction and history be tested independently. This is a project
recommendation, not a claim that current signals already satisfy RELAX.

Keep DLSS RR as a subsequent full-scene PT backend, using the existing narrow
Streamline boundary but a distinct PT input/output contract. The current Hybrid
Reflection RR integration writes a reflection-owned output and must not be reused
as a full-scene result followed by the same legacy reflection composite.

Do not add ReSTIR, radiance caching, adaptive sampling or another transport
estimator in this work. Preserve Native progressive PT as the reference.

## Sources And Version Policy

The local Streamline header declares 2.12.0 in
third_party/streamline-sdk-2.12.0/include/sl_version.h. Its DLSSDOptions and the
existing StreamlineAdapter.cpp are the local API baseline. The online RR guide
currently identifies a different release; it is conceptual guidance, not proof
that every described tag/option exists in this checkout. No SDK upgrade is planned.

NRD is pinned to v4.17.3, commit 792eff196afdd350fd9c3f862119017ccb438a0e.
The ignored source checkout is third_party/nrd-sdk-4.17.3. The dependency manifest
records source/header/helper hashes and intended build encoding. No NRD binary
or compiled shaders are present or linked. Native D3D12 dispatch integration is
selected; NRI is not acquired. Pin documentation to this revision and never mix
helpers from one release with a different binary.

Official input guidance consulted:

- [NRD integration and front/back-end helpers](https://github.com/NVIDIA-RTX/NRD/blob/v4.17.3/README.md):
  separate radiance signals, material-factor removal/restoration, backend-specific
  packing and hit-distance handling. RELAX and REBLUR do not share an identical
  hit-distance encoding. Use the selected version's helpers, not copied formulas.
- [Streamline RR resource guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md):
  noisy input color, diffuse/specular reflectance, shading normals, roughness,
  depth, dense motion and specular correspondence. Specular hit distance starts
  at the primary surface, not the camera. View depth and hardware depth have
  distinct tagging paths. These requirements motivate the conversion layer below.

## Native To Backend Gap Matrix

Native definitions remain authoritative in [Guide Contract](path-tracing-guide-contract.md).

| Existing Native resource | Current meaning | Required design action |
| --- | --- | --- |
| PathTracing.SceneColor | Progressive mean, not necessarily current noisy frame | Do not feed progressive history into a second temporal history; produce separate current-frame noisy color |
| NormalRoughness | World shading normal and effective roughness | Preserve Native; encode separately for the selected backend, including miss handling |
| ViewZ | Positive camera-axis depth; miss zero | Preserve units; convert miss sentinel and tag/encoding per backend, not as hardware depth |
| MotionVectors | Previous minus current NDC, including rigid object transform | Explicit unit/sign/Y conversion and current/previous projection contract |
| Albedo | Linear base color, alpha is first-sample hit validity | Not diffuse reflectance or specular reflectance; add material/F0 inputs |
| DiffuseRadianceHitT | First-lobe-classified weighted radiance; primary emission included | Audit NEE lobe attribution, remove stable terms, then backend-specific demodulation |
| SpecularRadianceHitT | First-lobe-classified weighted radiance | Preserve sampling probability in radiance; supply corresponding secondary-path distance |
| Both signal alpha channels | Identical camera-to-primary hitT | Never reinterpret as diffuse/specular secondary hit distance |
| Accumulation | Sum RGB and sample count | Reference only; never overwrite with denoised output |

Code anchors: Shaders/shaders_PathTracing.hlsl TracePath/CSMain;
Engine/RtPbrSurveyEnginePasses.cpp MakePathTracingPass/MakeToneMapPass;
Renderer/StreamlineAdapter.cpp EvaluateStreamlineWithSdk RR path.

## Proposed Neutral Frame Contract

Define a small PT-owned input description, not a general RenderGraph redesign.
No sl::, nrd:: or NRI types in App, Scene, public engine settings or shared CPU/GPU
data. SDK includes and dispatch/loading/support queries stay in adapters. Reuse
existing resource binding/lifetime patterns before adding abstractions. A future
DLL can wrap this boundary; do not implement a plugin loader now.

Each evaluated frame carries: frame/sample identity, render/output sizes, explicit
signal semantic version, camera matrices and previous matrices, motion convention,
sample offset convention, world-unit scale, exposure and one reset generation.
Resource records identify format, extent and semantic, not just a non-null pointer.

Proposed additions (names provisional):

- CurrentFrameRadiance: linear scene-referred HDR before accumulation/tone mapping.
- SurfaceValidity, BackgroundRadiance and StableRadiance: primary miss/background
  and primary emission/unlit contributions, associated with the same primary sample.
- MaterialBaseColor, Metallic, F0 and the selected backend's material factors.
- DiffuseSignal and SpecularSignal with documented primary-lobe attribution.
- Diffuse/Specular secondary distance, validity and sampled-lobe indicators.
- DenoisedSceneColor: distinct output, never an in-place Native/history overwrite.

Do not derive the missing material quantities from Albedo alone. Do not divide
primary emission by diffuse reflectance or use unbounded division near black
materials. Freeze the backend factor/epsilon policy and validate reconstruction
for black, dielectric, metal, grazing-angle and textured materials.

At bounce zero, current direct/NEE terms can enter the diffuse class independently
of sampled continuation lobe. Audit analytic, environment and emissive NEE terms
before calling this a physically meaningful diffuse/specular partition. Changes
must preserve total expected radiance, PDFs and MIS weights; a partition identity
alone cannot prove unbiased transport.

For distances, distinguish camera-to-surface, primary-surface-to-secondary-hit,
secondary miss, absent lobe sample, NEE visibility ray and deeper-path statistics.
Specify the chosen backend policy for each. Do not invent a generic shared alpha
encoding or average distances using radiance sampling-probability weights.

## Initial Sampling And Composition Policy

First prototype: renderScale=1, one path/pixel/frame, no checkerboard, opaque rigid
geometry, fixed exposure, no upscaler chain. Reject unsupported settings with an
explicit readiness reason; do not silently modify the user's saved Native preset.

One sample/frame avoids the known multi-sample guide/signal association mismatch,
but is not sufficient by itself: Native primary sample offsets are random per
pixel, whereas temporal camera contracts normally expose a frame-wide offset.
Before backend evaluation, validate an opt-in frame-coherent primary sampling
mode and matching guides/matrices/motion, or prove the selected backend supports
the existing sampling convention. Do not claim zero projection jitter means
zero stochastic primary offset. Native RNG/output defaults must remain unchanged.

NRD proposal: denoise only eligible surface transport, restore the same material
factors, then add stable emission/unlit and background exactly once. A primary
miss must not borrow a surface guide. Backend miss encoding/denoising range must
be pinned; do not feed ViewZ=0 blindly. Larger sample batches remain rejected
until a coverage and guide-association policy is separately validated.

RR proposal: supply coherent full current-frame noisy color with reflectance and
correspondence guides. Decide background/stable-layer treatment using the pinned
SDK contract and dedicated captures. RR is not an NRD diffuse/specular output pair;
do not apply NRD remodulation or add emission a second time after full-color RR.

## Graph And History Ownership

```text
PathTracingPass -> current-frame inputs -> PtDenoiserPreparePass
    -> PtDenoiserEvaluatePass -> PtDenoiserComposePass -> ToneMapPass
PathTracingPass -> Native progressive SceneColor -> ToneMapPass (default bypass)
```

Distinct resources are declared as real reads/writes and registered in Graph and
Debug Texture Preview. Native capture must not be replaced by display-mapped data.
NRD histories/pools are PT-backend-owned, not swap-chain or Hybrid Reflection
history. Restore descriptor heaps/state tracking after any external dispatch.
SDK absent, unsupported hardware, readiness failure or evaluate failure selects
Native output and reports the reason; never display stale backend output.

Backend history resets on scene load, camera cut, incompatible projection/sample
convention, resource size, material/geometry topology change, backend/config
change and manual reset. Ordinary camera/object movement should use reprojection,
not inherit Native's reset-on-every-camera-change blindly. Lighting changes need
a documented response policy rather than assuming every intensity edit is a cut.
Exposure conversion/history rescaling is deferred; exposure changes reset initially.

Pause freezes sample and backend-frame advancement and reuses the last completed
output. Forward step evaluates exactly once. Reset while paused invalidates history
without advancing RNG; the next successful submission consumes the reset. Frame
generation/history validity advances only after a successful submitted evaluation.

## Delivery Steps And Acceptance Gates

1. Pin one backend and freeze its semantic contract. Audit acquisition/build/SDK
   boundaries and decide native D3D12 dispatch versus an isolated NRI integration.
   Deliver version manifest and readiness reasons; do not add backend calls yet.
2. Add neutral material/stable/current-frame/distance outputs and numerical tests.
   Require Native-default regression and nontrivial signal reconstruction, including
   emissive direct/indirect, NEE/MIS and secondary miss/absent-lobe cases.
3. Validate the opt-in coherent sampling/motion/packing preparation path. Capture
   packed buffers and CPU reference comparisons; reject unsupported batch sizes.
   Predeclare tolerances from formats, retaining strict failures separately.
4. Add compile-safe default-off adapter and graph boundary, then one native-size
   backend dispatch. SDK-off builds and deterministic Native fallback must pass.
   Enforce resource states, lifetimes, descriptor restoration and failure reporting.
5. Validate reset/pause/step/resize/reprojection. Reuse history infrastructure but
   do not treat progressive-accumulation tests as backend-history certification.
6. Matched Native/Denoised A-B: same seed/timeline/exposure/ROI, independent
   high-sample Native reference, ROI mean error and temporal variance, edge leakage,
   disocclusion, motion/reversal/settling, highlight detail and emission/background.
   User joins visual review; a smoother result alone is not acceptance.
7. Commit a scoped acceptance report and profile Release separately. Keep default
   off until numeric and user review pass. RR/full-resolution-to-upscaled chaining,
   larger batches, SH modes and deformation are independent follow-up work.

Each numbered step is a small reviewable delivery; step 2 may be split into
material/stable outputs and secondary-distance commits. This document alone does
not authorize commit, push or default enablement. The user has selected NRD RELAX
and authorized starting its staged implementation.

## Open Decisions

- NRD RELAX-first is selected; RR remains a separate later backend.
- Version and native D3D12 integration are fixed in the dependency manifest.
- Freeze lobe attribution, secondary miss/absent-sample distance policy and emission
  exclusion before any readiness flag can become true.
- Finish the pending integrated GUI review when the user can unlock the desktop.

## Step 1 Progress

- [x] Obtain official v4.17.3 source and verify the exact commit.
- [x] Record hashes, license path, dependency references and proposed build options.
- [x] Add SDK-independent NrdRelaxAdapter support query to CMake and MSBuild.
- [x] Query remains available=false / NotIntegrated even with source installed.
- [ ] Build NRD library and compiled shaders: deferred until input preparation.
- [ ] Version/runtime/encoding checks in a real SDK adapter: deferred.

The shell is not wired into GUI/settings or frame evaluation. No SDK includes,
loading, GPU resource creation, feature enablement or dispatch occur. There is no
SDK-present runtime build to certify yet; current tests cover the SDK-free shell.
Missing material/stable signals, secondary distance, sampling correspondence and
GPU submission remain explicit readiness blockers, not assumed completed work.

Manifest: [nrd-relax-dependency-manifest.json](nrd-relax-dependency-manifest.json).
Reacquire the source without building dependencies:

```powershell
git clone --depth 1 --branch v4.17.3 https://github.com/NVIDIA-RTX/NRD.git third_party/nrd-sdk-4.17.3
git -C third_party/nrd-sdk-4.17.3 rev-parse HEAD
```

Require 792eff196afdd350fd9c3f862119017ccb438a0e and the manifest hashes.
Upstream CMake fetches ShaderMake and MathLib; resolve/verify their manifest
commits before running that build. No generated NRDConfig.hlsli, binary or compiled
shader hash is claimed yet. The upstream default normal encoding is not our
selected float-friendly encoding: future library and helper builds must both use
normal encoding 4 and roughness encoding 1, checked against GetLibraryDesc.

### Step 1 Validation

Debug x64 MSBuild of the CMake-generated NrdRelaxAdapterTests target and renderer
library passed (one existing Streamline LNK4199 delay-load warning, zero errors).
CTest: 28/28 passed. Python PT suite: 154/154 passed. Project/filter XML parsed,
all seven manifest source hashes matched, SDK checkout remained clean and ignored.
The standalone MSBuild project source registration was checked, but its full app
link was not rerun. No NRD binary was built/loaded and no new GPU or visual test
was performed. The pending user GUI review remains pending.

Build evidence: C:/work/RtPbrSurvey-agents/nrd-relax-shell-20261010.
Next delivery: neutral current-frame, material and stable-emission/background
outputs, with reconstruction tests before secondary-distance preparation.

## Step 2A: Neutral Frame Outputs

The Native pass now additionally writes the following render-size resources.
They are registered with RenderGraph, debug texture preview and native PTBUF
capture. Native SceneColor remains the ToneMap input; no denoiser is enabled.

| Resource | Format | RGB | Alpha |
| --- | --- | --- | --- |
| PathTracing.CurrentFrameRadiance | RGBA32_FLOAT | Current dispatch mean, before progressive accumulation | 1 |
| PathTracing.MaterialF0Metallic | RGBA16_FLOAT | First primary sample F0 = lerp(0.04, baseColor, metallic) | Metallic |
| PathTracing.StableRadiance | RGBA32_FLOAT | Mean directly visible primary emission or primary Unlit contribution | 0 |
| PathTracing.BackgroundRadiance | RGBA32_FLOAT | Mean primary miss contribution | 0 |

Material miss values are all zero. Albedo alpha remains the first primary sample
hit/miss indicator, not accumulated coverage. Current-frame RGB does not include
any prior accumulation. Stable includes Unlit baseColor plus enabled emission;
secondary emission/Unlit hits and environment transport remain in the existing
diffuse/specular signals. Turning emission off does not remove Unlit baseColor.

For radiance mode (debugOutput=3), reconstruct:

```text
filterableDiffuse = DiffuseRadianceHitT.rgb - StableRadiance.rgb
currentFrame = filterableDiffuse + SpecularRadianceHitT.rgb
             + StableRadiance.rgb + BackgroundRadiance.rgb
```

The subtraction is a future adapter conversion, not a new Native estimator.
Existing DiffuseRadianceHitT semantics, random draws, accumulation and ray
generation are unchanged. The RGBA16 legacy signals impose quantization tolerance
on reconstruction/subtraction; packing must avoid tiny negative residuals.
Current, stable and background use FP32 to preserve dynamic range and allow
numerical checks. This adds 56 bytes/pixel (~110.7 MiB at 1920x1080), excluding
allocation alignment. Memory/performance optimization is a later measured step.

The root signature now uses the 64-DWORD limit (18 tables, five root SRVs,
36 constants). Further outputs must consolidate bindings or move constants to
a CBV; adding more independent tables is not supported.

These are NOT ready-to-submit RELAX inputs. Remaining blockers include secondary
hit distance, diffuse/specular NEE partition audit, demodulation/NRD packing,
coherent primary sampling, backend dispatch and history. Material guides remain
first-sample values while color signals average the dispatch; require one path
per pixel/frame for initial integration. Non-radiance debug modes are outside
the reconstruction contract. No depth/normal/MV convention is changed here.

Validation runner: Tests/PathTracing/validate_denoiser_outputs.py. It checks
resource completeness, frame/format identity, finite values, F0, miss sentinels,
stable/background separation, reconstruction and one-frame accumulation. The
stable fixture independently checks two primary emission values. Unlit handling
is implemented in the shader but not GPU-validated: the current glTF loader
ignores KHR_materials_unlit and the engine currently initializes material flags
without Unlit. This step does not broaden importer/material semantics. Optional
--baseline compares all seven previous Native guide/accumulation captures
bit-for-bit, not just the displayed LDR image. GPU evidence is recorded separately
below; unit tests do not substitute for GPU captures.

### Step 2A Validation (2026-10-10)

- Debug x64 app build via CMake-generated MSBuild project passed, zero warnings
  and zero errors. First link attempt failed because the earlier agent-launched
  GUI app held the EXE; it was closed and the retry succeeded.
- CTest: 28/28 passed. Python PT suite: 164/164 passed.
- Roughness GPU cohort: 11 captures, all seven legacy Native guide/accumulation
  buffers and metadata bit-identical to the pre-change baseline. New output
  reconstruction passed (maximum absolute error 0.001922607421875, within the
  relative/absolute half-precision bound). All capture logs contained zero
  D3D12 errors/corruption messages.
- Primary-emission GPU cohort: eight captures, two visible emission values
  matched independently; reconstruction maximum error 0.00019532442092895508.
  Material/miss/background checks passed and capture logs had zero D3D12 errors.
- Progressive GPU cohort: two captures. CurrentFrameRadiance was bit-identical
  to the accumulation-off reference at the same sample index, while Accumulation
  contained history and its average differed from the current noisy frame.
  All three history-independence checks passed, with zero D3D12 capture errors.
- GPU: NVIDIA GeForce RTX 2080 Ti; driver 616.56. These are local input-contract
  tests, not denoising/performance results or cross-device validation.
- The original stable fixture run was retained as failed: it omitted the common
  Lighting emission toggle and incorrectly assumed glTF Unlit support. The
  corrected fixture enables both emission toggles and tests two emitters, not
  Unlit. No importer behavior was changed to make the test pass.
- The old roughness baseline runner reported nonzeroDiffuse=false because the
  current fixture has only metallic materials. Captures, sentinels and signal
  reconstruction were valid. That failed report is retained, not relabeled as
  passing; bitwise baseline comparison uses its actual seven captures.
- Files checked as UTF-8 without BOM/CRLF; git diff --check passed. GUI review
  remains pending because the desktop is locked. No NRD GPU dispatch occurred.

Evidence directories (ignored, not committed):

```text
bin/PathTracingValidation/nrd-step2-baseline-20261010
bin/PathTracingValidation/nrd-step2-outputs-20261010
bin/PathTracingValidation/nrd-step2-stable-20261010 (failed fixture)
bin/PathTracingValidation/nrd-step2-stable-retry-20261010
bin/PathTracingValidation/nrd-step2-progressive-20261010
C:/work/RtPbrSurvey-agents/nrd-step2-outputs-retry-20261010
```

Tested executable SHA256:
ef5eb3d7656d6584675e277ddb0a8ae77d02f8430c0f557de0e2386826dfafff.
Shader SHA256:
b4d854986dee0a39a4e7dae8b4d4778008eb20d166eb1d7ed44c5d9623320630.

Next: secondary-hit distance and lobe partition audit before NRD-specific packing.

## Step 2B: Secondary Ray Diagnostic And Lobe Audit

PathTracing.SecondaryRayData is RGBA32_FLOAT, registered for graph inspection,
debug preview and PTBUF capture. Like material guides, it records only the first
primary sample of the dispatch, not a batch average:

| Channel | Meaning |
| --- | --- |
| R | First continuation segment CommittedRayT; on miss the segment TMax |
| G | Sampling proposal: 0 untraced, 1 cosine/diffuse proposal, 2 GGX/specular proposal |
| B | Trace status: 0 untraced, 1 triangle hit, 2 miss |
| A | Reserved zero |

An all-zero tuple means no secondary ray was traced (primary miss, one-segment
limit, invalid BSDF sample, rejected geometry hemisphere, etc.). It does not mean
an environment miss. A miss has an explicit status and finite TMax, not infinity.
The distance starts at the biased primary-surface ray origin, not the camera and
not the exact unbiased surface. It is the first continuation segment, not total
path length and not distance to a NEE light. It must NOT be supplied to NRD as
separate diffuse/specular hit distance without completing the signal contract.

No random draw, ray or radiance operation was added/changed. To fit the additional
UAV table, two unused trailing root-constant padding DWORDs were removed from the
CPU/HLSL declarations; all used offsets remain unchanged. The root signature now
costs 63 DWORDs (19 tables + five root SRVs + 34 constants). Resource overhead is
another 16 bytes/pixel; total Step 2 output overhead is 72 bytes/pixel.

### Audit Findings

The direct-light, emissive NEE and environment NEE branches already split primary
contributions using EvaluatePathTracingBrdfComponents, with the same lighting
factor/PDF/MIS weight. Do not rewrite these as if they were still wholly diffuse.
Primary directly visible emission/Unlit remains intentionally in the legacy
diffuse signal, removed separately using StableRadiance in the proposed adapter.

The continuation path does have a classification limitation:

```text
SamplePathTracingBsdf.weight = (diffuseBRDF + specularBRDF) * cos / mixturePDF
primarySpecularPath = (sampled proposal was GGX)
all later contributions are assigned to that selected proposal's signal
```

Selecting a proposal is NOT selecting a physical transport component. For a
mixed material, either proposal evaluates both BRDF components. Their sum is the
correct Native estimator, but each separated continuation guide is generally
not an estimator of its named physical lobe. Perfect bitwise reconstruction of
the sum does not prove those individual guides are suitable for RELAX.

The metal-only roughness fixture masks this problem because the diffuse BRDF is
zero. Existing Native correctness acceptance concerns total radiance, not this
new denoiser component contract. Keep Native behavior/reference outputs intact.

Next narrow delivery: derive component-weighted continuation signals using the
same sampled direction and mixture PDF, retain shared downstream transport and
MIS/RR factors, and prove sum preservation plus an independently evaluated
mixed-material split. Do not select only the sampled proposal's BRDF with an
incorrect mixture PDF. Legacy guide semantics should remain explicit; new
backend signals need their own contract and distance association. Only then
proceed to coherent primary sampling and NRD demodulation/packing.

Validation runner: Tests/PathTracing/validate_secondary_ray.py. The fixtures check
no continuation at one segment, specular proposals on metal, secondary hits and
both proposals on mixed-material indirect geometry, finite distances, explicit
miss/untraced states and primary-miss sentinels. These state checks alone are not
an independent geometric distance oracle or a completed denoiser distance test.

### Step 2B Validation (2026-10-10)

Debug x64 app build passed with zero warnings/errors. CTest: 28/28 passed;
Python PT suite: 169/169 passed. GPU diagnostic validation passed all three
fixtures (six captures), with zero D3D12 capture errors. The indirect fixture
included 123628 secondary-hit pixels, 1147033 secondary misses and 802939
untraced pixels; both sampling proposals were present. One-segment and metal
proposal expectations passed separately. No NRD dispatch or GUI review occurred.

Diagnostic evidence: bin/PathTracingValidation/nrd-step2b-secondary-20261010.
Build evidence: C:/work/RtPbrSurvey-agents/nrd-step2b-secondary-20261010.
Executable SHA256:
3e1d07853778ec756213d9851d54ac856e0d95328df48d54e96dfccaa65578fb.
Shader SHA256:
4ec543775bebea3b838b4f3e1cb44042237488fe59b35e464dc5968872d6b17a.

Native regression evidence:
bin/PathTracingValidation/nrd-step2b-native-regression-20261010.
All 11 previous guide/frame/accumulation outputs and metadata were bit-identical
to Step 2A captures. Reconstruction and sentinel checks passed with the unchanged
maximum error 0.001922607421875; all 11 logs contained zero D3D12 capture errors.
This confirms the diagnostic/root-padding change did not change this fixture's
Native outputs; it does not validate physical mixed-lobe signal separation.

### Step 2B Resume (2026-10-11)

The diagnostic path was resumed after the Scene Editor UI work. Added rejection
tests for unknown assessment modes and nonfinite/nonpositive ray maxima. The
focused secondary-ray unit suite passed 7/7. CTest passed 28/28.

The current working-tree Python suite passed 170/171: its archived-emitter
geometry check rejects the manually edited large-emissive-panel scene. That
user-authored file was preserved, not reset or included in this delivery. When
only that file's test read was mocked with its committed HEAD contents, all
171 tests passed. This is explicitly a committed-fixture result, not a claim
that the modified working-tree fixture passed.

Secondary diagnostics passed three fixtures/six captures with zero recorded
D3D12 errors. Counts matched the previous run: the indirect fixture had 123628
hits, 1147033 misses and 802939 untraced pixels. The test still does not provide
an independent geometric distance oracle or completed RELAX distance packing.

Evidence: bin/PathTracingValidation/nrd-step2b-resume-20261011.
Native regression was also rerun: all 11 output buffers and metadata were
bit-identical to the 2026-10-10 baseline, with zero recorded D3D12 errors.
Reconstruction maximum error remained 0.001922607421875 and all checks passed.
Evidence: bin/PathTracingValidation/nrd-step2b-native-resume-20261011.
Executable SHA256:
55b12e8dcba7cf88dbdf249c54baf95a0dba7cbc50c6e1d9f9c9530ba51632a4.
Shader SHA256:
4ec543775bebea3b838b4f3e1cb44042237488fe59b35e464dc5968872d6b17a.

### Next: Step 2C Physical Continuation Split

- Preserve Native total radiance and existing guide semantics/reference files.
- Evaluate diffuse/specular BRDF components for the same sampled continuation
  direction, cosine and mixture PDF; proposal identity is diagnostic only.
- Apply component weights to shared downstream transport, including the same
  visibility, emission, MIS and Russian Roulette factors; do not sample a second
  path or change random-number consumption merely to separate signals.
- Give new backend signals separate names/contracts. Prove their sum reconstructs
  the original estimator within the storage precision bound, and compare a mixed
  material against an independently evaluated component reference.
- Define secondary-distance association and coherent primary sampling before
  NRD demodulation/packing or RELAX GPU dispatch. Step 2B's distance is not a
  substitute for that contract.

Manual integration checks 1 (rendering-path switch) and 2 (live edits/PT reset)
were confirmed by the user. Checks 3-5 remain deferred. NRD denoising quality,
performance and actual SDK dispatch remain unvalidated.

### Step 2C: Physical Continuation Signals (2026-10-11)

Added three independently named RGBA32_FLOAT diagnostic resources, registered
with RenderGraph and Debug Preview:

- PathTracing.PhysicalDiffuse: current-dispatch averaged physical diffuse RGB.
- PathTracing.PhysicalSpecular: current-dispatch averaged physical specular RGB.
- PathTracing.PrimaryContinuation: first primary sample's continuation direction
  in XYZ and mixture PDF in W; zero when no continuation was sampled.

The two radiance signals exclude primary emission/unlit and primary background.
Their alpha is reserved zero, NOT an NRD hit distance. Their sum plus
StableRadiance and BackgroundRadiance reconstructs CurrentFrameRadiance. Legacy
proposal-classified guides and Native radiance expressions remain unchanged.
This reconstruction contract applies to the Radiance output (debugOutput=3),
not the early-return Albedo/Normal/Emission diagnostic rendering modes.

For a primary continuation, evaluate both physical BRDF components for the SAME
sampled direction and divide each by their sum per channel. Apply these fractions
to every shared downstream contribution. The common cosine/mixture-PDF factor
cancels in this ratio; the original throughput, random consumption, MIS and RR
remain shared. Primary direct/NEE contributions use their existing BRDF split.
Proposal identity is retained only as a diagnostic, not a physical-lobe label.

One root descriptor table binds UAVs u13-u15 at descriptor offsets 0/2/4 because
the allocator stores SRV/UAV pairs. Contiguity is asserted; the root signature
now uses exactly 64 DWORDs. New storage costs 48 bytes/pixel, bringing the
Step 2 diagnostic additions to 120 bytes/pixel. Performance is not assessed.

The independent GPU oracle uses a single-sample, two-segment mixed-material
environment-miss fixture. It evaluates a double-precision BRDF from readback
guides and an independently reconstructed view ray. Readback normals are
renormalized after half-precision storage. It does not numerically validate the
recorded PDF or geometric hit distance, primary direct/NEE separation, deeper
paths or RR behavior. Multiple samples/frame still average radiance across
different primary hits while guides describe the first sample: not RELAX-ready.

Nine captures passed reconstruction, finite/nonnegative signals, reserved alpha,
both proposal types, both physical components and normalized directions.
Mixed-BRDF oracle: 16539 pixels, maximum fraction error 0.00027673653559151523
against an unchanged 0.003 bound. Reconstruction maximum error:
1.1920928955078125e-7. No D3D12 capture errors were recorded.

Evidence: bin/PathTracingValidation/nrd-step2c-physical-20261011.
The initial report failed the oracle because the readback normal was not
renormalized. report.json is retained; reassessment.json records the corrected
analysis and its script hash against the SAME captures, not a new GPU run.

Debug x64 build passed with zero warnings/errors. CTest passed 28/28. Python
suite passed 179/180 against the current working tree: the only failure remains
the manually edited large-emissive-panel fixture. Mocking only that file's read
with its committed HEAD contents passed 180/180; no user file was overwritten.
Initial sandbox-only failures were temporary-file access restrictions; these
results are from the subsequent unrestricted test execution.

Native comparison passed all 11 pre-existing buffers bit-for-bit, including
metadata, using the SAME current executable/camera with the previous HEAD PT
shader compiled separately. The original comparison with historical Step 2B
captures failed because camera target representation and matrices differed;
that failure was not accepted or hidden. An initial reference launch with a
missing compiled output was stopped and excluded from acceptance.

Evidence: bin/PathTracingValidation/nrd-step2c-native-comparison.json, with both
bytecode hashes, and nrd-step2c-matched-reference-retry-20261011/report.json.
The generic reference report's source shader hash still identifies the working
tree; the comparison manifest explicitly identifies the substituted bytecode.
All 11 logs recorded zero D3D12 errors. The final indentation-only shader cleanup
was compile-checked separately; GPU captures used pre-format bytecode.
Build evidence: C:/work/RtPbrSurvey-agents/nrd-step2c-physical-signals-20261011.

Next: define a coherent primary sample contract and lobe-associated secondary
distance semantics, then NRD demodulation/packing. No NRD SDK dispatch or
denoising-quality acceptance is included in Step 2C.

### Step 2D: Opt-In Pixel-Center Primary Sampling

Added pathTracing.pixelCenterPrimary (boolean, default false) to engine settings,
render preset serialization and validation identity. true selects (0.5, 0.5)
inside every pixel, for every sample/frame, with no projection jitter. The first
prototype requires samplesPerFrame=1. Native presets are never silently changed.
No GUI switch or automatic denoiser activation is added in this step.

The shader still consumes the same two primary RNG draws before substituting
the center coordinate. Continuation random sequencing and the default random
primary mode remain unchanged. Mode changes invalidate PT accumulation. The
former always-zero hitNormalSource root constant slot is reused for this flag;
the shared RayQuery normal-source selector remains explicitly fixed at zero.
Root constant count and the 64-DWORD signature budget are unchanged.

PTBUF metadata now records samplesPerFrame and primarySampling explicitly.
Independent ray reconstruction accepts pixel-center or random-per-pixel and
rejects unknown conventions. Mixed sampling cohorts and multi-sample batches
are rejected by the physical-component oracle. Legacy captures without these
fields remain interpreted as the documented random-per-pixel, single-sample
fixtures by these test helpers; this is not a general backend readiness policy.

QueryNrdRelaxSamplingReadiness qualifies only the sampling sub-contract. It
rejects non-single-sample or random-primary configurations, without mutating
settings. QueryNrdRelaxSupport remains unavailable even when sampling qualifies.
The readiness query is not yet connected to an evaluation path (none exists).

Pixel-center sampling is a conservative integration prototype, not a claim of
anti-aliasing quality or equivalence to stochastic Native convergence. It removes
primary stochastic anti-aliasing; secondary transport remains stochastic. Later
frame-wide jitter requires matching camera constants, motion and reset policy.

Secondary-distance work remains a separate delivery. The pinned NRD README
recommends the first indirect segment, excluding camera-to-primary distance.
Our mixture-sampled continuation contributes to both physical components, so
proposal identity cannot indicate a missing physical component. NEE visibility
distances, direct/indirect mixing, misses, absent continuations and deeper-path
statistics require explicit policies and geometric checks before packing. Do
not copy SecondaryRayData or reserved physical-signal alpha into RELAX unchanged.

#### Step 2D Validation (2026-10-11)

Pixel-center physical signals passed all nine captures, with zero D3D12 errors.
Independent mixed-BRDF oracle: 16536 pixels, maximum fraction error
0.0002767289026537334; reconstruction maximum error 1.1920928955078125e-7.
Evidence: bin/PathTracingValidation/nrd-step2d-physical-center-20261011.

Four pixel-center guide captures passed independent numeric checks: plane
NormalRoughness, shifted-projection ViewZ, marker Albedo (zero material
classification mismatches), and moving-camera MotionVectors. MV passed both
the fixed threshold and the existing half-precision bound. Evidence:
bin/PathTracingValidation/nrd-step2d-center-guides-20261011.

Default random-primary CurrentFrameRadiance was bit-identical to the Step 2B
roughness fixture with matching camera matrices (maximum error zero). All
previous metadata fields matched exactly; the two new sampling fields were
checked explicitly, not discarded without validation. This tests only that
radiance buffer, NOT all 11 Step 2C buffers. An attempted Step 2C comparison had
different camera target representation/matrices and was retained as failed.
Evidence: bin/PathTracingValidation/nrd-step2d-native-default-20261011,
matched-baseline-assessment.json (accepted) and report.json (failed comparison).
Camera compatibility must be checked for each future automation cohort.

Final app-only Debug x64 build passed, zero warnings/errors. CTest passed 28/28,
including changed settings/history/sampling-readiness tests. Python current
working tree passed 183/184; the known manual-emitter-fixture mismatch remains.
Mocking only its file read with committed HEAD contents passed 184/184. Manual
scene data and unrelated Transform UI edits were preserved.

The first build exposed the shared RayQuery normal-source reference after slot
reuse; it was fixed with the explicit constant-zero selector. Subsequent
ALL_BUILD attempts compiled but failed parallel post-build directory copies;
they are not passing build results. App-only final evidence:
C:/work/RtPbrSurvey-agents/nrd-step2d-app-final-20261011.
No RELAX packing, secondary-distance validation, dispatch or GUI acceptance was
performed. Next delivery remains the secondary-distance contract and oracle.

### Step 2E: First Secondary Geometry Oracle (2026-10-11)

Added an independent double-precision CPU oracle for positive-scale, unparented,
finite primitive planes. It reads JSON transforms and dimensions, reconstructs
pixel-center primary rays from the captured inverse projection, applies front-face
culling, and intersects the recorded continuation direction with the scene.
It does not infer intersection from GPU status, proposal identity, guide depth,
or radiance. Unsupported geometry is rejected rather than silently ignored.

PTBUF records rayTMin, rayTMax and normalBias from the active engine settings.
The fixture requests all three explicitly; the runner verifies they were applied
and requires identical settings/matrices/sample identity across all three buffers.
No estimator, shader transport, root signature or GPU resource was changed.

#### Distance Semantics

| Native diagnostic state | Meaning | Backend implication |
| --- | --- | --- |
| status=1 | First secondary committed hitT, from the biased continuation origin | Excludes primary camera distance but is not yet a validated NRD lobe distance |
| status=2 | Secondary miss; native diagnostic distance equals rayTMax | A sentinel, not an actual finite surface hit; backend mapping still required |
| status=0 | No first secondary trace; all SecondaryRayData channels zero | No measured distance; never fabricate one from camera-to-primary hitT |
| proposal=1/2 | Diffuse/GGX proposal used by the mixture sampler | Not proof that the other physical BRDF component was absent |
| W=0 | Reserved diagnostic channel | Must not be interpreted as validity, PDF, or packed distance |

The actual ray starts at O=A+normalBias*N_geometry. Its committed hit point is
B=O+hitT*D. Therefore the unbiased primary-surface-to-hit length is
length(normalBias*N_geometry + hitT*D), not simply hitT. This oracle verifies
both the raw ray parameter and the reconstructed unbiased length, using the
fixture's independently known geometric normal. The shading normal guide is
not a substitute for that geometric normal. No corrected-distance GPU resource
or physical-signal alpha is published in Step 2E.

The pinned NRD v4.17.3 README requires in-lobe distance, not merely a geometrically
correct mixture-sampled hit. Radiance component weights do not automatically
prove that a direction is an accepted in-lobe distance for both signals. Define
lobe-specific acceptance/fallback and missing-lobe handling before assigning
the same distance to both channels. Do not divide distance by PDF/BRDF weights,
include the primary camera distance, sum arbitrary deeper segments, or treat
direct-light visibility distance as this indirect segment.

#### Validation

Three GPU captures passed: SecondaryRayData, PrimaryContinuation, Albedo.
Independent oracle examined 75898 traced primary-hit samples: 9279 secondary
hits and 66608 misses accepted after fixed boundary exclusions. Excluded-edge
count 278 includes primary and secondary boundary masks; edge margin is 0.002
world units. Distance bound was fixed before GPU assessment:
1e-4 + 5e-5 * expected distance. Maximum hitT error was
0.00004530241258660084, maximum bound-normalized error 0.17455867601102432.
Unbiased reconstructed distance error was 0.00004530240977995703; the largest
normal-bias correction was 0.0009986770890693109. No D3D12 errors were recorded.

Unit tests reject primary-distance substitution, wrong hit/miss status, wrong
miss sentinel, changed bias/cohort, unsupported transforms/geometry and missing
buffers; they also test finite bounds, front-face culling and the edge mask.
Focused suite passed 9/9. Debug x64 build passed with zero warnings/errors;
CTest passed 28/28. Current Python tree passed 192/193 (the same manually edited
emitter fixture failure); committed-fixture-only read substitution passed 193/193.
No user scene or unrelated Transform UI edit was reverted or committed.

Evidence: bin/PathTracingValidation/nrd-step2e-secondary-geometry-20261011/report.json.
reassessment.json records the same captures analyzed after CRLF-only script
normalization, with the final analysis script hash; the original report is retained.
Build: C:/work/RtPbrSurvey-agents/nrd-step2e-secondary-geometry-20261011.
The evidence is a first-segment planar geometry audit, not arbitrary mesh,
multi-sample, deforming-geometry, in-lobe-distance, NEE/RR or NRD dispatch approval.
Next: publish explicit corrected distance/state, define the in-lobe policy, then
validate backend packing. RELAX remains unavailable.

### Step 2F: Corrected Secondary Surface Distance (2026-10-11)

Published PathTracing.SecondarySurfaceDistance as RGBA32_FLOAT, available through
Debug Preview and the RenderGraph resource outputs. R is the unbiased distance
from the primary surface to the first secondary hit. G is explicit state:
0=untraced, 1=hit, 2=miss. B/A are reserved zero. Miss and untraced distances are
zero; the state distinguishes them. This records the first primary sample, not
an average over samples per frame, and is not an NRD packed input.

The shader computes length((actualRayOrigin - primaryHitPosition) + D * hitT).
Using the actual GPU origin offset preserves floating-point rounding of the
normal-bias addition. Existing SecondaryRayData semantics and physical-signal
alpha remain unchanged. The new texture adds 16 bytes per pixel. Root table 25
now covers u13-u16 with descriptor offsets 0/2/4/6; the root signature remains
26 parameters and 64 DWORDs.

Four GPU captures passed with zero D3D12 errors. SecondaryRayData,
PrimaryContinuation and Albedo were bit-identical to Step 2E, including all
metadata. The independent plane oracle accepted 9279 hits and 66608 misses.
Published corrected-distance maximum error was 0.00004546315214870589 against
the same fixed distance bound. New negative tests reject biased hitT published
as surface distance and a finite distance published for a miss.

Focused geometry tests passed 11/11. The app-only Debug x64 build passed with
zero warnings/errors. Existing CTest binaries passed 28/28. Python current-tree
tests passed 194/195, with the known manually edited emitter-fixture mismatch;
substituting only that fixture read with committed contents passed 195/195.
Unrelated Transform UI and manual scene edits were preserved. Native total
radiance was not recaptured in this step; the exact regression comparison above
is limited to the three named diagnostic buffers.

Evidence: bin/PathTracingValidation/nrd-step2f-surface-distance-20261011/report.json.
Build: C:/work/RtPbrSurvey-agents/nrd-step2f-surface-distance-20261011.
Next: define in-lobe acceptance/fallback separately for diffuse and specular,
then validate pinned-SDK packing and material demodulation. A correct mixture
segment does not establish distance validity for both lobes. RELAX dispatch,
arbitrary-mesh distance validation and denoised quality acceptance remain pending.

### Step 2G: Lobe Distance Policy and Capture Audit (2026-10-11)

Audited PathTracingSampling.hlsli against the pinned NRD README's hit-distance
requirements. SamplePathTracingCosineHemisphere is cosine-weighted. The native
SamplePathTracingGgxHalfVector samples the GGX NDF, not a view-conditioned VNDF.
Rejecting below-surface directions does not turn that NDF sampler into VNDF.
Proposal ID 2 therefore cannot certify the selected VNDF integration baseline.

#### Chosen First Integration Policy

- Keep Native BRDF, mixture PDF, RNG consumption and physical radiance unchanged.
- Use a separate, deterministically seeded distance-sampling stream. No distance
  sampling may consume the Native transport stream or alter its throughput.
- Generate a cosine diffuse distance ray and a view-conditioned GGX VNDF
  specular distance ray at the same primary surface. Do not weight their distances
  by the physical radiance fractions, mixture PDF, or throughput.
- Initially trace both dedicated lobe rays rather than probabilistically skip
  one. This avoids claiming that the current nonzero two-component radiance split
  is a skipped-lobe estimator, and avoids relying on sparse reconstruction.
- Preserve separate hit/miss/untraced states until pinned-SDK miss encoding is
  verified. A diagnostic miss distance of zero is not ready for NRD packing.
- Below-geometric-surface, inactive-lobe and failed sample handling must be
  explicit and tested before dispatch. Do not resample without documenting the
  resulting distribution, or substitute primary camera distance.
- Treat current cosine-proposal distances as possible future diffuse reuse only.
  Reuse on specular-selected pixels requires a cosine fallback. Do not implement
  cross-lobe acceptance based solely on nonzero BRDF component weights.

The dedicated path costs up to two extra first-segment traces per primary hit.
Start with this inspectable baseline; optimize reuse only after its geometric,
sampling-distribution and miss-policy tests pass. VNDF implementation and its
reference must be chosen before adding the GPU sampler; no sampler was changed
in this step. Radiance demodulation and pinned RELAX packing remain subsequent
work, not implied by distance readiness.

#### Validation

Added audit_nrd_lobe_distance.py, which reruns the independent geometry oracle,
checks the source report's exact resource set and capture hashes, and rejects
unknown states/proposals and invalid corrected distance values over the full
image. It always reports nrdDistanceReady=false until the dedicated path exists.
The planar oracle's geometric proof still covers only its fixed ROI.

Replayed the Step 2F capture cohort, not a new GPU run: 833603 untraced pixels,
72160 diffuse-proposal hits, 717980 diffuse-proposal misses, 78159 GGX-NDF-proposal
hits and 371698 GGX-NDF-proposal misses. Six focused tests passed, including
unknown proposals/nonfinite values outside the geometric ROI, finite miss
distance rejection and refusal to mark diffuse-only inputs fully ready.
No runtime or shader source changed; no new build or quality result is claimed.
Evidence: bin/PathTracingValidation/nrd-step2f-surface-distance-20261011/lobe-distance-audit.json.
Next: implement and independently test the dedicated VNDF/cosine distance path.

### Step 2H: Isolated Cosine / VNDF Sampler (2026-10-12)

Added PathTracingDenoiserSampling.hlsli, separate from Native transport sampling.
It implements isotropic GGX visible-normal sampling from Heitz 2018 (corrected
2019), with local normal +Z, and a cosine-weighted local direction sampler.
Reference: https://jcgt.org/published/0007/04/01/ and its paper.pdf.
Roughness maps to alpha=max(roughness^2, 0.001), matching the existing Native
minimum, without changing that sampler. The VNDF normal PDF uses the exact GGX
distribution and Smith visibility ratio; no arbitrary PDF denominator floor is
introduced. Reflection density uses the half-vector Jacobian 1/(4*VdotH).

This is the unbounded Heitz VNDF baseline, NOT a bounded or NRD VNDF-v3 sampler.
Visible microfacet normals can reflect below the surface. The test shader marks
these directions invalid, with reflection PDF zero; it does not resample or
renormalize the remaining directions. Handling this missing-distance state,
including geometric versus shading-normal rejection, remains a dispatch blocker.
Do not treat the PDF as a normalized conditional above-surface distribution.

Input contracts reject nonfinite/invalid roughness, nonunit or below/tangent
views, and random coordinates outside [0,1). Roughness zero uses the documented
alpha floor, not an exact delta distribution. Helpers use local frames only;
world-frame construction and independent deterministic RNG are not connected yet.

#### HLSL Execution Tests

Added a standalone D3D12 compute test and CTest registration, without linking
SceneRenderer, Streamline or NRD. It runs 28672 HLSL samples: 20 valid
roughness/view configurations and eight invalid-input configurations, 1024 samples
each. Valid cases include roughness 0/0.1/0.35/0.7/1 and view cosine
1/0.8/0.1/0.0001, random zero and a near-one endpoint. Outputs are checked against
a double-precision reference, the normal-incidence analytic quantile, unit length,
visible-normal support, cosine directions/PDFs and rejection state. At roughness
one/normal incidence, exactly 512 samples reflect below the surface as expected.
This is pointwise/analytic validation, not a full statistical convergence proof.

The endpoint extension initially failed: subtracting squared disk coordinates
lost precision near the disk boundary. Equivalent nonnegative residual formulas
and precise intermediates fixed this without changing the sampling distribution
or loosening the pre-existing 0.0002 + 0.002*abs(reference) test bound.

Final WARP and default hardware-adapter runs both passed, each with 1539 rejected
below-surface samples and no D3D12 errors. Maximum reflected-direction errors:
WARP 0.00000788471, hardware 0.00000862977. Debug x64 test-target build passed
with zero warnings/errors; CTest passed 29/29 (existing test binaries plus the
newly built sampling target). Final build log:
C:/work/RtPbrSurvey-agents/nrd-step2h-sampler-delivery-20261012.
Native shader source, RNG and radiance are untouched;
no application radiance cohort or new scene-distance capture is claimed here.

Test invocation from repository root:

```powershell
& .\build\Debug\RtPbrSurvey.PathTracingDenoiserSamplingTests.exe .\build\Tests\shaders_DenoiserSampling_CSMain.cso
& .\build\Debug\RtPbrSurvey.PathTracingDenoiserSamplingTests.exe .\build\Tests\shaders_DenoiserSampling_CSMain.cso --hardware
```

Next: connect an independent deterministic distance RNG, world-frame conversion,
geometry-normal rejection and dedicated first-segment traces. Validate the new
directions/distances against the planar oracle and compare Native radiance in a
matched cohort. NRD miss mapping, material demodulation, packing and RELAX
dispatch remain pending; support is still unavailable.

### Step 2I: Opt-In Dedicated Distance Rays (2026-10-12)

Added denoiserDistanceDiagnostics (default false), persisted with the existing
scene/render-preset settings. The PT Debug section exposes Denoiser Distance
Diagnostics. Changing this setting resets accumulation. Root constant slot 3 is
now primarySamplingFlags: bit 0 selects pixel-center sampling and bit 1 enables
distance diagnostics. No extra root DWORD was added. Default random-primary
behavior is retained when bit 0 is clear, independently of the diagnostic flag.

At the first primary sample only, independent diffuse/specular streams use
HashUint(MakeRandomState(pixel, sampleIndex) XOR salt), with salts 0xa511e9b3 and
0x63d83595 respectively. Each consumes two random values, never the Native
transport state. Cosine and VNDF directions are transformed from the primary
shading-normal frame, then rejected if they point below the geometric surface.
The rays use the existing closest-hit/front-face-culling policy, rayTMin/rayTMax
and geometric-normal bias. Native throughput, mixture PDF and radiance do not
consume these results. Even with multiple primary samples, at most two additional
distance rays are traced per pixel/frame. The diagnostic ray-count upper bound
includes these rays.

#### Diagnostic Resources

Five new RGBA32_FLOAT render-size resources are registered in Debug Preview and
RenderGraph. They are allocated alongside the existing PT guides, including when
the diagnostic tracing is disabled; storage cost is 80 additional bytes/pixel.
Disabled tracing writes zero inputs; it does not incur the extra ray queries.
Root table 25 extends through u21 with the existing SRV/UAV pair stride, leaving
the 26-parameter, 64-DWORD root budget unchanged. CMake and the standalone MSBuild
project both track the new sampling header dependency.

| Resource | Channels |
| --- | --- |
| DenoiserDiffuseDirection | XYZ world direction, W cosine direction PDF |
| DenoiserSpecularDirection | XYZ world reflection direction, W VNDF reflection PDF or zero for invalid samples |
| DenoiserDiffuseDistance / DenoiserSpecularDistance | R bias-corrected surface distance, G state, B raw hitT or rayTMax for miss, A reserved zero |
| DenoiserPrimaryPosition | XYZ actual unbiased GPU primary hit position, W=1 for recorded primary hit; zero when disabled/miss |

Distance states: 0=unrecorded/primary miss, 1=hit, 2=miss, 3=invalid direction,
4=inactive material lobe. R is zero for all non-hit states. State 3 is not a miss
or a valid zero-distance contact; state 4 is not a probabilistically skipped lobe.
Diffuse is inactive for fully metallic or zero-albedo material; specular is
inactive if all material F0 components are zero. Directions remain diagnostics
even when their lobe is inactive. No automatic resampling is performed.

#### Geometry and Native Regression

Initial captures passed direction/PDF, state and Native ON/OFF checks but failed
the distance oracle near tangential rays: independent CPU/GPU primary-point
rounding differences were amplified. That failed report remains at
bin/PathTracingValidation/nrd-step2i-distance-rays-20261012/report.json.

Added actual primary-position capture rather than widening the secondary-distance
bound. Its position is independently checked against the camera/plane oracle
with a fixed component bound 1e-4 + 1e-5*max(1, abs(expected position)). Then the
recorded point's float32 bias addition is reproduced and its rays intersected
against the independent finite-plane geometry. Secondary raw and corrected
distance bounds remain 1e-4 + 5e-5*expected hitT. Sampling directions/PDFs are
separately checked against independently seeded double-precision references.

Eight final GPU captures passed with zero D3D12 errors. In the fixed planar ROI:
diffuse had 7672 hits, 73353 misses, zero invalid directions (10 boundary exclusions);
specular had 11111 hits, 62571 misses, 7327 invalid directions (26 exclusions).
Corrected-distance maximum errors were 0.000005273699283048217 and
0.000003998878208566481 respectively. Maximum direction errors were
0.0000005272970864250714 and 0.00001569781990107266.
CurrentFrameRadiance was bit-identical with diagnostics ON/OFF, with all metadata
matching except the explicitly checked diagnostic flag. This is a matched,
single-sample pixel-center cohort, not an all-buffer/historical baseline claim.
Evidence: bin/PathTracingValidation/nrd-step2i-distance-rays-position-20261012/report.json.

App-only Debug x64 build passed with zero warnings/errors:
C:/work/RtPbrSurvey-agents/nrd-step2i-position-capture-20261012.
Settings/history test targets were rebuilt and CTest passed 29/29, including
remaining existing binaries. The six focused Python tests passed. Current Python
tree passed 206/207 with the known manual-emitter-fixture mismatch; committed
fixture read substitution passed 207/207 without editing that scene. Unrelated
Transform UI changes were preserved. Standalone MSBuild project XML was checked;
that separate project was not rebuilt in this step.

The GPU fixture covers active lobes, rigid planes and one pixel-center primary
sample. Inactive-lobe branches, arbitrary meshes/normal maps, multi-SPF diagnostic
behavior, pauses and GUI visual acceptance are not approved by this cohort.
Next: settle SDK miss/invalid/inactive-lobe handling, then material demodulation
and pinned RELAX packing. A geometrically valid dedicated distance does not make
the current unbounded VNDF rejection policy NRD-ready. RELAX remains unavailable.
