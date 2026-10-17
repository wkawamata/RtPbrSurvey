# Path Tracer Denoiser Input Contract And Delivery Plan

Date: 2026-10-10. Base: main 0c175e3 (PR 96).
Branch: codex/pt-denoiser-input-contract.
Status: opt-in live input conversion, hit-distance reconstruction and input
admission and native D3D12 RELAX GPU dispatch diagnostics connected.
Diagnostic material restoration and scene-color composition are connected.
Explicit diagnostic final-image selection is connected, default Native.
Healthy-device NRD initialization/preparation fallback is connected.
Device loss/recording failures remain fatal; quality acceptance remains pending.

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
records source/header/helper hashes and intended build encoding. Optional SDK
CPU scheduling diagnostics build a static library from pinned sources; live
reconstruction compiles the pinned 3x3 kernel. Diagnostic GPU dispatch uses 27
externally compiled RELAX pipelines and verifies their SDK identifiers. NRI
is not acquired. Pin documentation to this revision and never mix helpers from
one release with a different binary.

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
The F0-only specular inactivity rule above was corrected in Step 2J below.

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

### Step 2J: Semantic Gate and Pinned SDK Packing Tests (2026-10-12)

Added SDK-neutral InspectNrdRelaxDistance at the adapter boundary. This is a CPU
reference/preflight decision, not a per-frame GPU conversion or dispatch path.
It accepts corrected distance/state/raw hitT/reserved data from the dedicated
distance resource, rayTMax and independently determined primary/lobe activity.
It must not be called on the legacy camera-hitT diagnostic as if that were a
dedicated lobe distance. A HitCandidate is not overall input readiness.

| Input | Decision |
| --- | --- |
| Positive hit distance, positive raw hitT within range, active lobe/primary | HitCandidate, preserve corrected distance |
| Hit distance above 65504 | PackingRangeExceeded, require explicit range policy |
| Canonical finite-range miss | EnvironmentMissPolicyRequired, do not substitute rayTMax as NRD hit distance |
| Invalid direction | ReplacementRayRequired, not a valid zero-distance contact |
| Unrecorded primary hit | TraceRequired |
| Unrecorded primary miss | ExcludePrimaryMiss, keep background outside denoising |
| Confirmed inactive lobe, zero distance/raw hitT | InactiveLobe, require RGB zero as well as distance zero |
| NaN/Inf, negative values, unknown/fractional state, nonzero reserved data, contradictory activity | InvalidData |

A ray-range miss alone does not establish an environment miss: geometry can
exist beyond rayTMax. Establish a conservative scene/ray-range guarantee before
choosing an environment-distance encoding. Do not borrow SIGMA's occluder-miss
rule without checking RELAX's own signal semantics. Invalid independent distance
samples can coexist with nonzero physical radiance; sanitizing their distance to
zero is not a reconstruction/coverage policy. Both conditions still block the
prototype's input conversion. Positive distances close to zero are retained;
an exact zero hit remains rejected by this strict prototype gate.

#### Actual SDK Helper Test

Added optional RTPBRSURVEY_TEST_NRD_PACKING (default OFF). Enabling it requires
the exact source commit 792eff196afdd350fd9c3f862119017ccb438a0e and unmodified
Shaders/NRD.hlsli. The original header is copied byte-for-byte into the generated
test directory, with a repo-owned test-only NRDConfig.hlsli. The test shader calls
the real RELAX_FrontEnd_PackRadianceAndHitDist, not a rewritten approximation.
SDK/NRI runtime libraries and production shader configuration are not enabled.
No NVIDIA types/headers were added to Engine/App/Scene public interfaces.

Eight exact GPU cases cover ordinary radiance/distance, NRD_INF saturation to
65504, zero/negative/NaN/Inf distances, invalid RGB clearing and component-wise
RGB range clamping. Both hardware and WARP passed with zero D3D12 errors.
Sanitization only protects numeric range; the zero and invalid cases demonstrate
why it cannot certify semantic input validity. The original and generated header
SHA256 matched: 17c59c9b7e96a4957d0ac20a4d22dfc914ce4367fc3f4432a0928a94dc2bdad0.
The test configuration is not a substitute for matching the eventual NRD library
and its encoding configuration.

```powershell
cmake -S . -B build -DRTPBRSURVEY_TEST_NRD_PACKING=ON
& .\build\Debug\RtPbrSurvey.PathTracingDenoiserSamplingTests.exe .\build\Tests\NrdPacking\shaders_NrdRelaxPacking_CSMain.cso --packing --hardware
```

SDK-OFF CMake configuration also passed with a deliberately nonexistent SDK path
(only the expected unused-variable warning). The local configuration was restored
to SDK-test ON. Fresh SDK tests do not download, modify or commit vendor artifacts.

#### Zero-F0 Specular Regression

The prior producer treated all-zero F0 as inactive specular. That is incorrect
for the current Schlick Fresnel model: grazing-angle specular energy can remain.
The diagnostic producer now keeps specular active; diffuse inactivity remains
fully metallic or zero base color. This does not change Native BSDF/RNG/radiance.

Five black-metal GPU captures passed: Albedo, both distance buffers and diagnostic
ON/OFF CurrentFrameRadiance. All 1324948 primary-hit pixels had inactive diffuse
and active/invalid specular states, never specular state 4. Specular states were
182106 hits, 1025770 misses and 117072 invalid directions. There were 1090880
primary-hit pixels with nonzero Native radiance; ON/OFF radiance was bit-identical.
Zero D3D12 errors. This tests lobe activity, not new distance geometry or denoised
quality. Evidence: bin/PathTracingValidation/nrd-step2j-black-metal-20261012/report.json.

Debug app and adapter/sampling test-target builds passed with zero warnings/errors.
CTest passed 30/30 with SDK tests enabled (newly rebuilt changed targets plus
remaining existing binaries). Nine focused Python tests passed; current tree
passed 209/210 with the known manual-emitter-fixture mismatch, and committed
fixture read substitution passed 210/210. User scene and unrelated UI changes
were preserved. Build logs: C:/work/RtPbrSurvey-agents/nrd-step2j-sdk-packing-20261012,
nrd-step2j-state-policy-20261012 and nrd-step2j-black-metal-20261012.

Next: implement environment-miss range proof and invalid-direction replacement
or a validated bounded-sampling/coverage policy, then connect material
demodulation and SDK packing to actual GPU input conversion. RELAX dispatch and
quality acceptance are still unavailable; this step does not enable them.

### Step 2K: Range Proof and Replacement Coverage Audit (2026-10-12)

Added BuildNrdRelaxWorldBounds and an optional NrdRelaxRayRangeContext to the
adapter's CPU reference gate. Bounds include the actual referenced vertices of
every supplied scene instance. Indexed BLAS geometry uses global packed vertex
indices, not only the range's declared local vertex interval; unindexed geometry
uses its firstVertex interval. Legacy empty range lists use the same whole-mesh
range-zero fallback as the renderer. The first three rows of stored transposed
InstanceData::world match the TLAS 3x4 transform.

Nonfinite vertices/matrices, non-affine matrices, invalid ranges/mesh IDs/indices,
incomplete triangles and float-overflow transforms invalidate the whole bound.
An empty/missing scene is not certified. Double-precision bounds are inflated by
32*float-epsilon*max(1, sum of absolute transform terms) per coordinate. This is
a conservative prototype margin, not a universal proof about arbitrary GPU
deformation or hardware intersection error.

Canonical misses remain EnvironmentMissPolicyRequired by default. They become
EnvironmentMissCandidate (distance 65504, validated against the pinned packer)
only when all these checks pass:

- Supplied bounds are complete, finite and ordered.
- The caller explicitly verifies geometrySnapshotMatches; default is false.
- Actual biased ray origin/direction are finite; direction length is within
  2e-5 of one, matching the diagnostic unit-direction contract.
- rayTMax*length(direction) strictly exceeds the distance to the farthest AABB
  corner from that origin, including a 64*float-epsilon arithmetic margin.

The caller must verify the current uploaded vertices/indices and TLAS transform
snapshot (or a conservative superset), not merely CPU scene-pointer equality.
This CPU helper is NOT wired to an engine upload-generation check, GPU packing
or dispatch yet. Do not set the verification flag true for an arbitrary current
UI scene. The bound only rules out geometry beyond rayTMax under the existing
opaque/front-face-culling/TMin trace policy; it does not repair near clipping,
transparency, numerical intersection failures or missing GPU geometry.

Tests cover transformed/scaled geometry, multiple instances, referenced vertices
outside a declared range, legacy ranges, unknown/stale snapshots, insufficient
ray range, zero/nonunit/NaN directions, outside origins, invalid indices/mesh IDs,
projective matrices, nonfinite vertices and empty scenes. A distant indexed
triangle at z=200 correctly prevents classifying a rayTMax=100 miss as environment.

#### Invalid-Direction Replacement Plan

Do not silently retry the unbounded sampler and retain its old PDF: retries
condition the distribution and require a corresponding PDF/acceptance contract.
Investigate bounded VNDF as a separate sampler/PDF change, not a blanket claim
that all geometric-normal rejections disappear. The pinned SDK also provides
AREA_3X3/AREA_5X5 hit-distance reconstruction with pre-pass requirements.

Added audit_nrd_replacement_coverage.py as a necessary-condition audit before
enabling such reconstruction. It checks the passing source report, exact capture
hashes/cohort and scene hash, maps recorded primary positions to finite-plane
interiors, excludes ambiguous labels, and searches same-plane 3x3/5x5 witnesses
without wrapping screen edges. It cannot overwrite the source evidence files.
It always leaves inputReady=false and runtimeGeometrySnapshotVerified=false.
No distances, PDFs, radiance or GPU shaders are modified by this audit.

Replayed the existing Step 2I cohort, not a new GPU capture. Diffuse had no invalid
directions. Specular had 117072 invalid samples:

| Candidate seeds | 3x3 holes | 5x5 holes |
| --- | --- | --- |
| Verified hit samples only | 69215 | 49301 |
| Hits plus hypothetical verified environment misses | 46 | 46 |

The remaining 46 positions lacked an unambiguous plane-interior label. They are
not proof of a real SDK reconstruction failure: normals and SDK weights were not
executed. Conversely, 117026 neighbor witnesses are not sufficient proof of valid
reconstruction. This supports testing a 5x5 reconstruction path after range
verification, with explicit handling/validation at edges, rather than enabling it
unconditionally. Need matched NormalRoughness/material data and actual SDK kernel
results before accepting it. Evidence:
bin/PathTracingValidation/nrd-step2i-distance-rays-position-20261012/replacement-coverage.json.

Six coverage tests passed, including tiny images, edge wrap rejection, different
surfaces, unknown labels and holes. Current Python tree passed 215/216 with the
known manual-emitter fixture mismatch; committed fixture read substitution passed
216/216. No user scene or unrelated UI changes were modified. CTest passed 30/30
with the changed adapter target rebuilt and other existing binaries. Debug app
build passed; adapter-only linking emitted the expected LNK4199 unused Streamline
delay-load warning (no imports). No new PT image, GPU distance or RELAX quality
result is claimed. Logs:
C:/work/RtPbrSurvey-agents/nrd-step2k-delivery-tests-20261012 and nrd-step2k-delivery-app-20261012.

Next: bind bounds to verified engine upload/TLAS snapshots, collect matched normal
guides to resolve edge coverage, and test actual reconstruction/input conversion.
Material demodulation, production SDK configuration and RELAX dispatch remain
pending. The new CPU candidate decision alone never enables RELAX.

### Review Follow-up: Integration Safety Guards (2026-10-12)

The Q3 static review reported no defect in the current default-off contract,
but identified latent descriptor-layout and configuration reuse risks. The
test-only NRD configuration now uses normal encoding 4, matching the frozen
future library policy. This is still a test configuration, not a runtime SDK
configuration; the radiance/distance packing helper does not consume normals.

The physical-signal UAV pair-layout invariant is checked with a runtime error
in all build configurations, including repeated descriptor initialization.
Root-signature cost is calculated from the actual parameter array before
serialization, instead of a hand-written constant assertion. Current cost
remains 64 DWORDs (20 tables, five 2-DWORD root SRVs, 34 constants). The earlier
Q4 claim that root SRVs cost one DWORD was incorrect and is not adopted.

No ray sampling, radiance, distance semantics or RELAX availability changes.
Next remains engine upload/TLAS snapshot verification and matched normal-guide
coverage before actual reconstruction/input conversion.

Validation: CMake Debug application and denoiser-sampling target rebuilt
successfully, including the real pinned SDK packing shader with encoding 4.
Focused CTest passed 3/3 (NrdRelaxAdapter, PathTracingDenoiserSampling,
NrdRelaxPacking). No application GPU capture or new RELAX quality result is
claimed. Unrelated Scene Editor and manually edited validation-scene changes
were preserved.

### Step 2L: Uploaded Geometry and Recorded TLAS Snapshots (2026-10-13)

NrdRelaxGeometrySnapshot owns a geometry-only immutable copy (vertices, indices,
ranges). CreateSceneGeometryBuffers uploads directly from that copy; material
and texture state remains on the original scene. Bounds use this uploaded mesh
and the exact instance prefix passed to a successfully recorded TLAS build.
AccelerationStructureResources now reports whether it recorded a build command;
unsupported devices and empty/missing resources do not certify new bounds.

Each upload, TLAS recording attempt and scene release invalidates previous
contexts. A monotonically increasing token is checked again when the distance
gate consumes a snapshot-created context, so even an already-created context
cannot validate a later TLAS generation. Contexts borrow snapshot storage and
must not outlive the engine/snapshot owner. Access is single-threaded. Empty or
invalid geometry produces token zero and cannot certify environment misses.

This certifies CPU provenance and command-recording order, not fence completion.
Future conversion must use the captured token and actual biased ray coordinates
for the same command-list/TLAS generation, after the existing build UAV barrier.
No conversion consumer is enabled yet; no dispatch, reconstruction, distance
encoding or radiance change is claimed. CPU scene-pointer equality is not used.

Lifecycle tests cover immutable upload copies, valid contexts, stale contexts
after another recording, stale token requests, distant transformed instances,
invalidation, release, missing uploads, replacement uploads and empty TLAS input.

Validation: Debug application and NrdRelaxAdapterTests rebuilt successfully.
CTest passed 30/30, including the new snapshot lifecycle checks; the remaining
test targets used existing binaries. Adapter-only linking emitted the known
LNK4199 unused Streamline delay-load warning. No new application GPU capture,
fence-completion proof or RELAX quality evaluation was performed. Geometry-only
CPU retention adds a vertex/index/range copy for the lifetime of uploaded scene
resources; arbitrary deformation or external mutation of GPU buffers is outside
this static-geometry contract.

### Step 2M: Matched Normal Guide Coverage Audit (2026-10-13)

The dedicated-distance capture runner can now collect NormalRoughness with
--normal-guide. The replacement audit's --require-normal-guide requires the
recorded resource hash, RGBA16 format and exact same capture metadata (apart
from resource/format). A separate or stale normal capture is not accepted.
Labelled pixels are checked against independently transformed fixture plane
normals, not merely against neighboring guide values.

Candidate witnesses additionally require unit-normal error <= .005, normal
dot >= .99 and roughness delta <= .02. These are explicit audit thresholds,
not the pinned SDK reconstruction weights. Opposite/invalid normals and
roughness boundaries cannot supply witnesses. Unknown/ambiguous plane labels
remain excluded: matching normals alone cannot certify geometry identity.
All source PTBUF files are protected from output overwrite.

inputReady and runtimeGeometrySnapshotVerified remain false. No reconstruction
kernel, material demodulation or production input conversion is enabled. The
snapshot provenance implementation from Step 2L does not automatically certify
historical/offline captures. Material identity and actual SDK weighting still
require validation before dispatch.

Validation: nine replacement-audit tests and nine dedicated-distance tests
passed. Legacy evidence replay retains the earlier coverage counts. A fresh
nine-capture cohort passed distance/direction checks, with zero D3D12 errors
and bit-identical Native diagnostic ON/OFF radiance. 1,324,466 labelled pixels
passed the independent normal check (minimum dot approximately 1; maximum
unit-length error .00048828125). This uniform-roughness planar fixture does not
exercise real material/normal-map discontinuities; synthetic tests cover guide
normal and roughness boundary rejection.

Specular invalid count remains 117072. Hit-only holes are 69215 (3x3) and
49301 (5x5). With hypothetical verified environment seeds, 47 holes remain,
all lacking a valid surface label. The earlier cohort had 46; these are
separate captures and no guarantee of identical floating-point edge labels is
claimed. Normal checks do not eliminate this unresolved edge population.

Evidence: bin/PathTracingValidation/nrd-step2m-normal-coverage-retry-20261013/
report.json and replacement-coverage.json. The initial cohort is preserved at
nrd-step2m-normal-coverage-20261013 with status failed: first Albedo camera
matrices differed from the other captures. Cohort checks were not relaxed.
No root cause for that initial camera transition is asserted. Next: explicit
edge policy and actual pinned-SDK reconstruction/input conversion evaluation.

### Step 2N: Pinned Reconstruction Kernel Build Contract (2026-10-13)

Added opt-in RTPBRSURVEY_TEST_NRD_RECONSTRUCTION (default OFF; requires the
existing packing test option). It compiles the unmodified upstream
RELAX_HitDistReconstruction.cs.hlsl for BOTH/RADIANCE in 3x3 and 5x5 modes,
with the same test encoding-4 configuration. No hand-written replacement
kernel is substituted. MathLib v11 is fixed to
aa432ad9a80691eb8c3ec4d576bbad5ac100fdc8, matching the dependency manifest;
the local source checkout is under the ignored SDK's External/MathLib folder.
Configure rejects mismatched commits or tracked shader-source modifications.

The reflection tests verify 8x8 thread groups, t0 Tiles, t1 NormalRoughness,
t2 ViewZ, t3 Specular, t4 Diffuse, u0 Specular output, u1 Diffuse output and
the 712-byte constants at b0/space1. Future D3D12 CBV storage must round this
size up to the required 256-byte alignment; do not copy a 712-byte logical
layout into an unaligned CBV allocation. These tests compile/reflect only.
They do not execute the SDK kernel, link the NRD runtime or enable RELAX.

#### Edge and Unresolved-Distance Policy

- State-3 invalid directions are missing-distance markers for reconstruction,
  not contact hits. Do not replace them with environment distance 65504.
- A real environment miss needs the matched range/snapshot proof from Step 2L.
- SDK reconstruction must use matched NormalRoughness and ViewZ. It uses its
  own bilateral/normal/Gaussian weights; the Python audit thresholds are not
  equivalent. Material IDs are not used by this upstream reconstruction pass.
- Zero distance can remain when no acceptable neighbor contributes. Early-out
  sky/out-of-range pixels do not write outputs, so the execution harness must
  clear output textures and distinguish excluded pixels from active lobes.
- Pending a validated mixed-history policy, any unresolved active-lobe primary
  pixel prevents accepting the reconstructed frame: use Native for that frame,
  invalidate denoiser history, and report the unresolved count. Inactive lobes
  and primary misses may remain zero according to their separate contracts.
- Never hide the 47 unlabelled audit pixels by relaxing labels or inventing a
  distance/PDF. The full kernel test must measure its actual output there.

Next execution tests: uniform seed preservation, missing-center reconstruction,
all-missing neighborhoods, screen borders, depth discontinuities, opposite
normals, sky tiles, out-of-range ViewZ and RGB preservation. Both neighborhood
modes must run on WARP first, then hardware with D3D12 error checks. Follow with
the matched captured cohort before accepting any production conversion path.

Validation: both upstream reconstruction modes compiled and passed reflection
checks. The checker rejected a packing-only shader as an invalid reconstruction
kernel. CTest passed 32/32 with two new reflection tests; other targets used the
existing verified binaries. SDK and MathLib remain source-only local artifacts;
no SDK shader source edits or runtime application dispatch changes were made.

### Step 2O: Actual SDK Reconstruction on WARP and Hardware (2026-10-13)

Added an opt-in standalone D3D12 texture harness for the unchanged upstream
3x3/5x5 kernels. Test-only C++ constants expand RELAX_SHARED_CONSTANTS from
the pinned SDK; static assertions check size 712 and relevant field offsets.
The CBV allocation is 768 bytes. Five input SRVs and two output UAVs use the
verified ABI. Uploads, clear-to-dispatch UAV barriers, readback transitions,
fence waiting and debug-layer error checks are explicit.

Nine synthetic 16x16 cases per mode verify all output pixels for finiteness,
RGB preservation and expected distances: uniform seeds, missing center,
all-missing neighborhoods, center ViewZ discontinuity, screen corner,
sky tile early-out, out-of-denoising-range ViewZ, one seed two pixels away
(visible only in 5x5), and opposite-normal neighbors. Sky/out-of-range outputs
remain the explicitly cleared zero; active all-missing RGB is preserved with
distance zero, demonstrating that unresolved distance must not be confused
with an excluded pixel or a valid contact hit.

Opposite-normal neighbors still reconstruct the missing center from their
constant distance: the SDK normal weighting is soft rather than the audit's
hard dot cutoff. This is observed SDK behavior, not a claim that cross-surface
reuse is correct. The uniform-distance cases cannot validate relative weights
between different distances or real scene material boundaries.

Initial execution exposed a test-harness ClearUnorderedAccessViewFloat error:
its CPU handle referenced the shader-visible heap. Added a separate CPU-only
clear descriptor heap; final runs have no D3D12 errors. WARP and NVIDIA GeForce
RTX 2080 Ti each passed both modes (9 cases per mode). CTest passed 34/34;
the new harness was rebuilt and other targets used existing verified binaries.

No captured-scene replay, material demodulation, production NRD dispatch or
denoising quality acceptance is claimed. Next: replay matched captured inputs
through this kernel, measure actual unresolved pixels, then implement guarded
conversion/Native fallback without silently certifying historical ray misses.

### Step 2P: Captured Geometry Replay Harness (2026-10-13)

Extended the isolated SDK execution harness to rectangular images with explicit
raw-input sizes. The Python replay runner verifies capture hashes, scene hash,
resource formats, exact metadata cohorts, plane normals and ViewZ computed from
actual captured primary positions. The capture runner now includes both
NormalRoughness and ViewZ when --normal-guide is requested.

Replay RGB is a constant 1/2/3 probe, not physical lobe radiance or demodulated
material output. Captured hit distances are retained; invalid directions remain
zero. Two separately labelled variants test hit-only seeds and hypothetical
environment misses encoded as 65504. Both leave inputReady=false and
runtimeGeometrySnapshotVerified=false. This does not certify historical misses.
PT primary misses have ViewZ zero; replay explicitly converts them to 1e6 to
exclude them from NRD's denoising range. Outputs are checked for finiteness,
range, RGB preservation and cleared excluded pixels before recording counts.

Two initial capture attempts failed exact camera-cohort checks and are retained.
Inspection found that UpdateFreeLookKeyboard rewrote gazePoint even with no
input whenever the app was foreground, replacing a distant scene target with
a unit-distance target and slightly changing projection arithmetic. Idle input
now leaves the camera untouched. Regression tests preserve the original scene
target and retain keyboard/mouse roll behavior. No cohort tolerances were relaxed.

The fixed-camera cohort passed all ten captures with zero D3D12 errors and
bit-identical Native diagnostic ON/OFF radiance. Independent ViewZ maximum
error was 3.4104e-5; 1,324,467 labelled normals passed. This new cohort has
46 invalid-direction pixels without surface labels (not the prior cohort's 47).

Both WARP and RTX 2080 Ti passed four full-resolution replay conditions, with
matching unresolved counts and unchanged probe RGB:

| Specular invalid directions (117072 total) | 3x3 unresolved | 5x5 unresolved |
| --- | --- | --- |
| Hit seeds only | 69187 | 49275 |
| Hypothetical environment seeds | 0 | 0 |

All active unresolved counts for the hit-only variant remain large, including
real misses deliberately left unencoded; they are not all sampler failures.
In the hypothetical variant, all active pixels have nonzero output distance.
The SDK also fills the unlabelled edge population, unlike the conservative
Python same-plane audit. This is NOT proof that their neighbors belong to the
correct surface, nor permission to bypass material/history safety checks.

WARP/hardware outputs are not bit-identical: maximum absolute distance
difference is 2.3842e-6 in hit-only tests and .0625 with 65504-valued environment
seeds. These are recorded observations, not a universal error-bound approval.
No demodulated lobe radiance or denoiser quality metric was evaluated.

Evidence is under bin/PathTracingValidation/:
nrd-step2p-stable-camera-cohort-20261013,
nrd-step2p-replay-warp-20261013 and nrd-step2p-replay-hardware-20261013.
The failed pre-fix cohorts nrd-step2p-viewz-cohort-20261013 and
nrd-step2p-viewz-cohort-retry-20261013 remain preserved.
Next: actual material/lobe demodulation and guarded GPU input conversion using
the current TLAS snapshot/range proof, then production NRD dispatch/history.

Final verification: Debug application, camera regression target and replay
harness built successfully. Five replay-input/output gate tests passed;
CTest passed 34/34, with the camera and replay targets rebuilt and other
targets using existing verified binaries. An initial sandbox temp-directory
permission failure in the Python fixture test was resolved by host-side
execution, without relaxing test assertions. User Scene Editor and manual
scene changes remain untouched; generated captures/raw buffers are not Git
content.

### Step 2Q: Material Demodulation Contract and SDK Round Trip (2026-10-13)

The pinned NRD_MaterialFactors helper is now executed in the optional GPU
contract harness. Project inputs are explicitly diffuseReflectance = linear
baseColor*(1-metallic), Rf0 = lerp(.04, baseColor, metallic), the original linear
material roughness and unit shading normal/view vectors. This matches the
project's diffuse reflectance and specular F0 decomposition; it is a frozen
integration convention, not a claim that the SDK helper removes every angular
BRDF/PDF term from the current Monte Carlo estimator.

Eight deterministic cases cover colored dielectric, black dielectric, colored
metal, black metal at grazing incidence, mixed metalness, smooth/rough limits
and exactly grazing view. GPU factors are compared against an independent
double-precision implementation of the pinned RTG rational environment-term
equation. Dividing physical test signals by factors, calling real RELAX packing
and multiplying by the SAME factors restores RGB and preserves hit distance.
The helper's factor floor is .02 and specular roughness scale floor is .1.
Black/fully metallic diffuse signals remain zero, not NaN; black-metal specular
is not declared inactive merely because F0 is zero.

Added InspectNrdRelaxRadiance as a CPU reference gate. It rejects nonfinite or
negative physical RGB, nonpositive/nonfinite/>1 material factors, and nonzero
inactive-lobe RGB. Demodulated values above 65504 yield PackingRangeExceeded
with no partial output; do not rely on SDK sanitization/clamping to hide HDR
energy loss. The 60000/.02 case proves why originally representable physical
RGB may require Native fallback after demodulation. Valid inactive lobes return
zero separately from active but zero-valued samples.

This does not yet convert actual PT GPU buffers. Stable primary emission,
unlit terms and background remain separate from demodulated lobes. Future
front/back conversion must use identical factors and original roughness (not
denoiser roughnessAA), preserve coherent per-pixel guides, and derive view
direction from primary rays for both perspective and orthographic cameras.
HDR overflow requires an explicit fallback or separately validated reversible
exposure policy. RELAX support remains unavailable.

Validation: Debug application and changed adapter/sampling tests rebuilt.
CTest passed 35/35 (new material-factor case plus existing tests; unchanged
targets used existing binaries). WARP and the hardware path each passed all
eight factor/packing round-trip cases without D3D12 errors. The CPU gate covers
valid division, inactive zero/nonzero signals, the exact 65504 boundary, HDR
overflow, invalid factors, negative RGB and NaN/Inf. No new real-material
capture, GPU conversion pass or RELAX output image was produced in this step.

### Step 2R: Guarded SDK GPU Conversion Function (2026-10-13)

Added isolated NrdRelaxInputConversion.hlsli. It computes pinned SDK material
factors, demodulates both physical lobes and packs their validated distances.
The app's general PT shader and public C++ interfaces do not include SDK shader
headers; only the opt-in test currently compiles this backend-specific function.

It checks finite/unit normal and view vectors (normalization after a .005 length
check), finite baseColor/F0/metalness/roughness in [0,1], nonnegative finite RGB,
positive finite factors <=1, and finite distances in [0,65504]. Diffuse activity
is derived from the current material model; specular remains active even for
black metal. Inactive diffuse requires zero RGB AND zero distance. HDR overflow
or any invalid lobe rejects the whole pixel without publishing partial packed
signals. Stored factors must be reused for later remodulation.

conversionReady only means this arithmetic conversion succeeded. Active zero
distance remains a missing-distance marker needing reconstruction; it is not
denoiser readiness or a certified hit. Input ray-state semantics, environment
range proof, TLAS generation, primary-hit mask and coherent sample metadata must
be checked by the eventual caller. Stable emission/unlit/background are not
passed through this function. No frame fallback/history policy is enabled yet.

Eight GPU cases validate ordinary conversion, black dielectric, colored metal,
black metal at grazing incidence, HDR overflow, NaN normal, negative radiance
and NaN distance. Accepted RGB round trips and hit distances are checked;
rejected pixels must have zero packed outputs and explicit status codes.
Next: execute this function against actual captured physical lobe/material
buffers, then wrap the same kernel in a guarded render-graph conversion pass.

Validation: changed shader/test harness built in Debug. WARP and hardware each
passed all eight guarded conversion cases without D3D12 errors; CTest passed
36/36 with the changed sampling harness rebuilt and other targets using prior
verified binaries. Actual captured physical-lobe buffers and production pass
bindings were not exercised. GPU bounds are checked on float arithmetic before
packing; the CPU reference uses double division and can be more conservative
at a float-rounding boundary. No bitwise CPU/GPU threshold equivalence is claimed.

### Step 2S: Actual Captured Physical-Lobe GPU Conversion (2026-10-13)

The dedicated runner's --material-guides now collects NormalRoughness, ViewZ,
MaterialF0Metallic, PhysicalDiffuse and PhysicalSpecular alongside the existing
geometry cohort and Native ON/OFF comparison. Thirteen captures passed with
unchanged Native radiance and no D3D12 errors. replay_nrd_conversion.py verifies
unique capture records/hashes, metadata/format agreement, reserved channels and
primary masks. View vectors come from the inverse-projected primary rays,
supporting the same perspective/orthographic construction rather than assuming
camera-to-point vectors. This cohort is still the uniform mixed-metal plane
fixture; it is not a textured/material-boundary quality test.

The test harness accepts bounded structured input records (seven float4 fields)
and writes four float4 conversion outputs per pixel. Whole-pixel status, excluded
primary misses, factor bounds, physical RGB remodulation and exact distance
preservation are assessed. No accepted primary pixels is rejected as incomplete
validation; partial output from rejected pixels and false inactive labels cannot
pass. Input/output alias paths are rejected, including existing equivalent files.
The prototype input count is bounded to a single legal 65535-group X dispatch.

Initial hardware output was all zero while WARP had succeeded. The shader's
Root SRV GetDimensions-dependent guard was replaced with an explicit CPU-supplied
count. Final hardware and WARP runs pass. A 17-record smoke test includes one
excluded primary miss and checks the non-64-multiple dispatch tail on both
devices; it is registered with CTest on WARP. The failed hardware report remains
preserved rather than silently overwritten.

1,324,948 primary pixels were accepted, zero rejected, and 748,652 primary misses
were excluded, for both hit-only and hypothetical-environment variants. Maximum
RGB remodulation error was 3.36e-8 on WARP and 7.13e-8 on hardware, with zero
D3D12 errors. Both reports retain inputReady=false and
runtimeGeometrySnapshotVerified=false. Factors operate on the captured RGBA16
material guides, so this does not establish equivalence to unquantized material
inputs or a correct spatially filtered result.

Evidence under bin/PathTracingValidation: nrd-step2s-material-cohort-20261013,
nrd-step2s-conversion-warp-final-20261013 and
nrd-step2s-conversion-hardware-retry-20261013. The initial failed hardware result
is nrd-step2s-conversion-hardware-20261013. CTest passed 37/37 and four new Python
conversion-assessment tests passed; the changed GPU harness was rebuilt, other
test targets used prior binaries. No production RenderGraph conversion pass,
conversion-to-reconstruction chain, runtime SDK library or RELAX denoising
dispatch was enabled. Next: chain the actual converted lobe signals through
SDK reconstruction, then integrate guarded production conversion/dispatch.

### Step 2T: Captured Conversion-to-Reconstruction Chain (2026-10-13)

replay_nrd_converted_reconstruction.py connects the actual Step 2S converted
Diffuse/Specular RGB and distances to the pinned SDK 3x3/5x5 reconstruction
harness. It requires exactly the two conversion variants, matching capture
report hashes, unchanged raw input/output hashes, regenerated input agreement,
and a recomputed conversion assessment with zero rejected primary pixels.
Duplicate, missing, stale or contradictory conversion records cannot pass.
Primary misses are excluded with ViewZ=1e6; the output must remain zero there.
Packed RGB must be bit-preserved at written pixels. Remodulation uses the same
captured conversion factors, not independently recomputed factors.

Both WARP and hardware passed all four variant/kernel combinations without
D3D12 errors. Maximum physical RGB round-trip error remained 3.36e-8 on WARP
and 7.13e-8 on hardware. Specular invalid-direction pixels numbered 117,072;
hit-only left 69,187 unresolved with 3x3 and 49,275 with 5x5. Hypothetical
environment encoding left zero unresolved, but that is not runtime range proof
or evidence of spatially correct reconstruction. The 46 unlabelled invalid
pixels and cross-surface reconstruction risks remain outside quality approval.
Counts agreed between devices; distance outputs are not claimed bit-identical.
The historical conversion executables differed between device reports; their
report/raw hashes are retained, and both use the same captured source cohort.

Evidence: bin/PathTracingValidation/nrd-step2t-chain-warp-20261013 and
nrd-step2t-chain-hardware-20261013. Three focused chain tests plus four previous
conversion tests passed. No C++ or shader rebuild was needed for this step;
existing reconstruction binaries were used and hashed. inputReady and
runtimeGeometrySnapshotVerified remain false. This is an isolated GPU chain,
not a production RenderGraph pass, full RELAX denoising or temporal history.
Next: establish guarded production conversion and live snapshot/range readiness
before wiring full SDK denoising dispatch and remodulated composition.

### Step 2U: Whole-Pixel Conversion Eligibility (2026-10-14)

InspectNrdRelaxPixel combines the existing distance/radiance checks for both
lobes. Invalid data, packing overflow, missing/stale geometry snapshots,
untraced signals and unresolved environment policy produce no partial packed
output. Active lobes must refer to the same live snapshot token source and
generation. Inactive lobes require the canonical inactive distance state and
zero physical radiance. Primary misses require clean distance diagnostics and
zero physical signals; they do not consume material factors or geometry state.

Convertible and ReconstructionRequired return both candidate packed lobes.
The latter retains a zero distance marker and explicitly does not grant
denoising readiness. Environment distance 65504 is returned only when the
existing per-ray range proof passes. The CPU snapshot token certifies recorded
upload/TLAS provenance, not GPU fence completion. This SDK-neutral helper is
not called by a production conversion pass yet and does not enable RELAX.

Focused tests cover stale and mixed snapshots, incomplete traces, environment
range failure, reconstruction markers, inactive contradictions, overflow,
nonfinite data, primary exclusions and suppression of partial outputs.
The Debug adapter test target was rebuilt successfully and CTest passed 37/37;
other test targets used existing binaries. No full application build or new
GPU scene capture was performed for this CPU contract step.
Next: connect the gate's contract to GPU conversion diagnostics/resources;
retain fail-closed runtime support until dispatch ordering and live inputs are
verified. Step 2Q-2T were committed separately as d78cf70.

### Step 2W: Contract-Aligned Test Vectors And Report Aggregation (2026-10-14)

Added a deterministic JSON corpus:
Tests/PathTracing/nrd_pixel_contract_vectors.json. It contains small
CPU pixel-contract vectors with unique case IDs. Host-only categories
(snapshot token, scene-range proof, trace provenance) are explicitly
marked hostOnly=true and gpuRepresentable=false; they must never be
reported as GPU proof.

Added a fail-closed report aggregator:
Tests/PathTracing/aggregate_nrd_reports.py. It validates required
contract/conversion/reconstruction/converted-reconstruction reports,
schema/status, uniqueness, source/report/raw hashes where available,
expected variant and kernel-mode coverage, D3D12 error counts, and false
input-ready/runtime-geometry flags. It rejects missing, duplicated, stale,
or contradictory evidence. Its aggregate status distinguishes CPU-only,
GPU arithmetic, SDK reconstruction, hypothetical environment, and
production runtime. Production runtime remains unavailable/not integrated.

Focused tests:
- Tests/PathTracing/test_nrd_pixel_contract_vectors.py
- Tests/PathTracing/test_aggregate_nrd_reports.py

Exact commands:

cd C:\work\RtPbrSurvey-work-3
python Tests\PathTracing\test_nrd_pixel_contract_vectors.py
python Tests\PathTracing\test_aggregate_nrd_reports.py
C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe -m unittest discover -s Tests\PathTracing
.\bin\x64\Debug\RtPbrSurvey.NrdRelaxAdapterTests.exe
ctest --test-dir build -C Debug -R "NrdRelaxAdapter|NrdInputConversion"
python Tests\PathTracing\aggregate_nrd_reports.py --input-dir <report-directory> --output <report-directory>\aggregate-report.json

Limitations:
- The corpus is a minimal shared adapter, not an invasive refactor of the
  C++ or HLSL test binaries. Existing hardcoded vectors remain; the JSON
  corpus records their contract IDs and conservative fail-closed policy.
- GPU vectors cover only semantics representable in the isolated conversion
  contract: rejected pixels must not carry partial packed outputs, and the
  CPU host gate remains the owner of snapshot/range/trace provenance.
- Hypothetical environment encoding remains hypothetical. inputReady and
  runtimeGeometrySnapshotVerified remain false.
- No production rendering, RenderGraph, production shader, SDK linkage, or
  RELAX availability changed.

### Step 2X Review Corrections (2026-10-14)

The initial CPU corpus runner silently skipped missing expectedDisposition
and accepted duplicated IDs. The GPU subset test only classified JSON cases
and invoked the unrelated fixed eight-case shader. Both review findings are
corrected. The CPU loader requires the exact eight known IDs, the five numeric
cases and three host-only metadata cases, schema/type/shape checks, finite
numeric values, and expectedPacked for convertible/reconstruction cases.
Malformed input fails with diagnostics; zero executed cases cannot pass.
Regressions mutate the corpus for missing/duplicate cases, changed expected
disposition/packed values, absent output expectations and malformed numbers.

Tests/shaders_NrdPixelContract.hlsl uses the existing isolated D3D12 structured
buffer harness. Python serializes the JSON physical radiance, material factors,
distances, primary mask and lobe activity into the seven-float4 input ABI,
dispatches WARP and compares the four-float4 output by case ID. The shader uses
the same InspectNrdGpuSignal and SDK packing helpers as the conversion function.
Factors are supplied from the CPU corpus: this validates signal arithmetic and
packing, not NRD_MaterialFactors computation. Snapshot, missing-trace and
reconstruction-state cases are excluded from this arithmetic-only GPU subset.
The two baseline GPU cases are valid hits and a primary miss. Mutations verify
that changed input/expectations fail, and invalid/overflowing single-lobe data
suppresses both packed outputs. InputReady and runtime geometry certification
remain unchanged; production RELAX is still unavailable.

Validation: the two focused Debug targets built successfully; Python corpus
and GPU subset tests passed 17/17, including actual WARP dispatch. Targeted
CTest NrdRelaxAdapter/NrdInputConversion passed 2/2. No hardware rerun, full
application build or new scene capture was performed. The prior real aggregate
reports still fail for a missing authentic contract report; that remaining
reporting work is not claimed complete by these review corrections.

### Step 2Y: Executed Contract Report And Runtime Conversion Diagnostics (2026-10-14)

write_nrd_contract_report.py now executes the CPU corpus with its explicit
path, verifies unique case output and successful exit, and dispatches the JSON
GPU subset on WARP. The schemaVersion=2 report records five executed CPU cases
and two GPU signal-arithmetic cases, case/domain IDs, executable/shader/corpus/
script hashes and the CPU output hash. It grants no GPU proof of host geometry
provenance. The aggregator requires these execution fields, exact seven-case
coverage, zero errors and false readiness flags; empty contract metadata no
longer passes. Output paths preserve existing evidence rather than overwrite it.

The contract report and real captured conversion/reconstruction chain aggregate
passed. Evidence: bin/PathTracingValidation/nrd-contract-20261014/contract-report.json
and aggregate-report.json; final producer/aggregator output is under
nrd-contract-final-20261014. A fresh constant-RGB reconstruction replay against
the same material cohort is nrd-report-reconstruction-20261014, avoiding reuse
of a converted chain report as a different proof domain.

The application now has an opt-in PT NRD Conversion Diagnostics RenderGraph
pass after PathTracingPass and before ToneMap. It reads current NormalRoughness,
Albedo, F0/metallic, physical Diffuse/Specular, distance diagnostics and primary
position. Separate render-size RGBA32F textures hold NrdDiffuseCandidate,
NrdSpecularCandidate and NrdConversionDiagnostics. They are registered with the
normal resource lifecycle and Debug Preview; graph declarations manage input
SRV/output UAV transitions. History clear includes all three outputs. Pause
retains the last frame; resize uses the existing render-resource recreation.
The final image continues to use Native PT SceneColor.

Enable this prototype in CMake (default OFF):

```powershell
cmake -S . -B build -DRTPBRSURVEY_BUILD_APP=ON -DRTPBRSURVEY_BUILD_TESTS=ON -DRTPBRSURVEY_TEST_NRD_PACKING=ON -DRTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS=ON
cmake --build build --config Debug --target RtPbrSurvey
```

This stage uses the existing pinned NRD shader-helper configuration. No NRD
runtime library is linked. SDK includes are isolated to the shader helper;
Engine interfaces remain SDK-neutral. The optional shader is deployed by the
existing CMake runtime-shader copy path. The default MSBuild/CMake configuration
does not enable this prototype.

Use one sample/frame, Pixel Center Primary and Denoiser Distance Diagnostics.
Open the three PathTracing.Nrd* resources from Debug Preview/RenderGraph.
Diagnostic R codes: 0 convertible candidate, 1 invalid, 4 primary miss,
5 untraced, 6 environment range policy required, 7 reconstruction required,
8 incompatible sampling, 9 missing current trace/snapshot, 10 distance diagnostics
disabled. G/B contain lobe arithmetic statuses for evaluated candidates, or
distance gate codes for rejected pixels. A remains zero: no denoiser readiness
is certified. Finite environment misses are not silently encoded as 65504.
Invalid/pending-policy data suppresses both outputs; state-3 candidates retain
the zero-distance reconstruction marker. The snapshot gate checks recorded
current upload/TLAS state, not a completed-fence or per-ray environment proof.

Validation: Debug app build succeeded with the option ON. Eleven matched live
captures on RTX 2080 Ti passed independent WARP SDK arithmetic comparison for
53,808 candidate pixels; maximum packed differences were 9.54e-7 Diffuse and
4.30e-6 Specular. Counts: 43,221 convertible, 10,587 reconstruction candidates,
748,652 primary misses, 1,271,140 environment-policy rejects; zero invalid or
untraced pixels. All rejected outputs were zero, all readiness channels zero,
and no D3D12 errors were captured. A separate distance-diagnostics-OFF run
verified gate 10 and zero candidates for all 2,073,600 pixels. Evidence:
nrd-runtime-conversion-20261014 and nrd-runtime-disabled-20261014 under
bin/PathTracingValidation. Existing buffer initial-state warnings remain.
Python report/corpus checks passed 29/29 and CTest passed 37/37 (other unchanged
test binaries were reused). The runtime-shader manifest initially rejected the
optional shader; it is now registered through the standard shader build helper
with explicit include/dependency arguments, and the manifest test passes.

```powershell
python Tests/PathTracing/write_nrd_contract_report.py --output <new-directory>/contract-report.json
python Tests/PathTracing/validate_nrd_runtime_conversion.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_runtime_conversion.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <another-new-directory> --disabled-distance-diagnostics
```

Next: validate the runtime environment range/provenance policy and guarded
reconstruction, then connect full RELAX SDK dispatch/history/composition. Runtime
conversion and the isolated contract/reconstruction tests do not establish
denoising quality, movement/resize acceptance or support availability.

### Step 2Z: Runtime Environment-Miss Range Gate (2026-10-16)

Step 2Y was committed as b4b5461. The next change exports the recorded geometry
snapshot bounds only for a matching, nonzero expected generation. Float bounds
are rounded outward with nextafter; unrepresentable values fail closed. CPU
tests check containment of the double bounds, zero/stale generations and
invalidation after another TLAS recording.

Each graph build clears the PT generation stamp; recording the current PT
dispatch stamps the snapshot token. Conversion exports bounds against that
stamp, and host gate 9 rejects a missing trace or mismatched snapshot. Bounds
and validity are copied into root constants, so later CPU scene changes cannot
mutate an already recorded conversion dispatch. Direction textures are added
as explicit graph reads, with the corresponding SRV/UAV state transitions.

For canonical environment misses, GPU conversion requires finite positive PDF,
unit direction, finite ordered scene bounds and sufficient rayTMax. It bounds
the farthest scene corner from primary position, conservatively expanded on
each axis by abs(normalBias), then adds a float arithmetic safety margin. This
contains the geometric-normal-biased ray origin without using a quantized
shading normal as a substitute. Only a passing miss becomes distance 65504;
insufficient range stays gate 6 and suppresses both outputs. Missing directions
remain reconstruction markers rather than environment rays. These checks use
recorded command ordering, not a new fence-completion certificate or complete
RELAX input-readiness claim.

The runtime validator now captures both direction textures and uses an
independent double-precision primitive-plane corner/range oracle. Successful
candidates are compared with isolated SDK conversion, with environment
encoding enabled only in the comparison of approved pixels. The long-range
fixture passed on RTX 2080 Ti: 1,324,948 candidate pixels, including 1,201,690
Diffuse and 1,025,770 Specular approved misses. Counts were 1,207,876 normal
candidates, 117,072 reconstruction candidates, 748,652 primary misses and zero
policy rejects/invalid/untraced pixels. Maximum packed error was 1.44e-6 Diffuse
and 7.16e-6 Specular. Evidence: nrd-runtime-range-20261016 under
bin/PathTracingValidation. No D3D12 errors were captured.

The validator accepts --ray-max and --expect-policy-rejects for a short-range
negative run. Full RELAX dispatch and runtime reconstruction are still pending;
Native remains the final image and all readiness channels remain zero.

The initial rayTMax=4 negative fixture produced only primary misses and was
correctly retained as failed (the intended policy gate was not exercised).
Retrying with rayTMax=6 and --policy-only exercised 416,200 policy-rejected
pixels; both candidate outputs were zero there and readiness stayed zero.
That four-capture smoke run checks rejection only, not arithmetic on any other
pixels. Evidence: nrd-runtime-short-range-retry-20261016; the failed initial
run remains nrd-runtime-short-range-20261016. Both runs had no D3D12 errors.
The rebuilt adapter and related CTest targets passed 3/3.

```powershell
python Tests/PathTracing/validate_nrd_runtime_conversion.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --ray-max 6 --policy-only
```

### Step 2ZA: Fail-Closed Runtime Policy Report Checks (2026-10-16)

Step 2Z was committed as 54fcc85. Before connecting reconstruction, the
policy-only report validator now independently rejects malformed or empty
capture shapes, non-finite values in any input, noncanonical primary markers,
primary-hit/exclusion contradictions, readiness claims, missing range rejects,
and candidate leakage on every excluded pixel (including primary misses).
Accepted reconstruction candidates remain permitted; this check does not
certify their arithmetic or reconstruction quality.

Five Python regression tests cover successful counts and the negative cases.
The retained RTX 2080 Ti four-capture short-range cohort also passes the stronger
validator with 416,200 rejected pixels and 416,886 primary hits. This is replay
of existing evidence, not a fresh GPU run. No rendering code or SDK dispatch
changed in this step. Full live reconstruction and RELAX remain pending.

```powershell
python -m unittest discover -s Tests/PathTracing -p test_nrd_runtime_policy.py -v
```

### Reconstruction Preparation: Live Guides (2026-10-16)

Step 2ZA was committed as ef33071. The opt-in conversion pass now also writes
three render-size RGBA32F resources through the existing allocation, resize,
clear, graph transition and Debug Preview paths:

- PathTracing.NrdNormalRoughness: pinned SDK front-end encoding 4, linear
  roughness encoding 1, world-space normal, material ID zero.
- PathTracing.NrdViewZ: positive linear view depth in R; unused channels zero.
  Excluded pixels use 1e6 as an explicit far-depth sentinel.
- PathTracing.NrdValidity: R is an accepted candidate, G is convertible without
  distance reconstruction, B requires distance reconstruction, A remains zero.
  These are preparation masks, not full RELAX input-readiness flags.

Only conversion-accepted pixels (diagnostic code 0 or 7) populate the guides.
Invalid source normals, roughness, depth or reconstructed view direction fail
closed with code 11; radiance candidates remain zero as well. Other excluded
pixels have zero normal/mask and far depth. Future reconstruction must honor
the mask and denoising range; the sentinel alone does not establish integration.
Pause retains the previous guides just like conversion candidates. The normal
history-clear pass clears all resources before the next conversion dispatch.
No SDK reconstruction or RELAX dispatch is added; Native remains final output.

The live validator captures seven resources with matched metadata, compares
the SDK best-fit signed normal representation and unchanged linear roughness,
requires exact ViewZ copies, and checks masks and excluded-region suppression.
It rejects malformed, non-finite and contradictory data. Three new Python
tests exercise successful data and corrupted guides; eight runtime Python
tests and three related CTest tests pass. Debug app build succeeds.

RTX 2080 Ti validation passed with seven fresh 1920x1080 captures in
bin/PathTracingValidation/nrd-runtime-guides-20261016: 1,324,948 accepted pixels,
117,072 requiring distance reconstruction, and 748,652 excluded pixels. All
capture logs had zero D3D12 errors (the existing two buffer initial-state
warnings remain). This is guide encoding/mask validation, not denoising quality
or interactive pause/resize acceptance. Full CTest initially passed 34/37;
three file-save tests failed inside the sandbox and all three passed when
rerun outside that restriction. Other unchanged test binaries were reused.

```powershell
python Tests/PathTracing/validate_nrd_runtime_guides.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python -m unittest discover -s Tests/PathTracing -p test_nrd_runtime_*.py -v
```

### Live Hit-Distance Reconstruction: Step 3 (2026-10-16)

The opt-in RTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS CMake option connects the
unmodified pinned SDK 3x3 RELAX HitDistReconstruction kernel after PT conversion.
It requires the existing conversion diagnostics and reconstruction test options.
The runtime wrapper only selects diffuse+specular / 3x3 and includes the SDK
kernel. SDK constant layout is isolated in Renderer/NrdReconstructionConstants.cpp;
Engine receives neutral bytes, with a 712-byte layout checked at compile time
and padded to a 768-byte per-frame upload allocation.

The graph pass is named PT NRD Hit Distance Reconstruction 3x3. It consumes
NrdNormalRoughness, NrdViewZ, NrdSpecularCandidate, NrdDiffuseCandidate and
NrdTiles. The tile texture is R32F at ceil(renderSize/16), regenerated on resize;
all tiles are active (zero), with invalid pixels excluded by the prepared far
ViewZ. No tile-classification optimization is claimed. The denoising range is
1000, matching the isolated replay harness, and the viewport origin is zero.

Outputs PathTracing.NrdReconstructedSpecular and NrdReconstructedDiffuse are
RGBA32F Debug Preview resources. Every dispatch clears both outputs and inserts
UAV barriers before the SDK call, so early-outs retain zero instead of stale
history. Graph dependencies handle conversion UAV to reconstruction SRV and
output transitions. Constants use the current frame slot after its existing
fence wait; their upload state and persistent lifetime are explicitly registered.
Pause retains prior results; history clear covers all added textures. Interactive
pause/reset/resize acceptance is reserved for Step 4.

These are raw SDK reconstruction outputs, not certified full-denoiser inputs.
RGB remains demodulated center radiance. Missing active distances may remain
zero, and inactive diffuse lobes can acquire an irrelevant neighbor distance;
the latter must be sanitized before full RELAX input admission. Neither full
RELAX history/dispatch nor final-image composition is enabled. Native PT remains
the final image and QueryNrdRelaxSupport remains unavailable.

The runtime validator captures ten matched resources, replays them through the
isolated 3x3 SDK harness on the same GPU, and requires bit-exact agreement plus
RGB preservation, finite bounded distances and zero excluded outputs. It reports
resolved/unresolved state-3 distances separately from inactive lobes. A separate
disabled-distance run requires zero validity and zero reconstruction outputs.
Python regression tests reject output corruption, invalid masks and malformed
arrays. Shader reflection checks the actual runtime wrapper's 5 SRVs / 2 UAVs,
8x8 groups, b0 space1 and 712-byte SDK constant layout.

The initial WARP comparison in nrd-runtime-reconstruction-20261016 failed its
5e-4 + 2e-5 relative threshold for 17 specular-distance pixels. RGB matched
exactly; maximum absolute differences were 0.0625 specular and 0.015625 diffuse.
Replaying the same inputs on RTX 2080 Ti matched both runtime outputs bit for
bit. The failed WARP report and outputs are retained, with same-device probe
outputs in its hardware-replay subfolder. Same-device bit equality is the
runtime integration gate; no relaxed WARP threshold was introduced.

The final positive report in nrd-runtime-reconstruction-final-20261016 passed
with ten fresh captures and same-device RTX 2080 Ti replay. Both lobes matched
bit for bit over 1,324,948 written pixels. All 117,072 missing specular distances
were reconstructed; zero remained unresolved. RGB was preserved, and capture
and replay logs contained no D3D12 errors. A separate three-capture run in
nrd-runtime-reconstruction-disabled-20261016 passed with distance diagnostics
disabled: validity and both reconstruction outputs were zero over 2,037,120
pixels. The two runs used their recorded dimensions independently.

```powershell
cmake -S . -B build -DRTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS=ON -DRTPBRSURVEY_TEST_NRD_RECONSTRUCTION=ON -DRTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS=ON
cmake --build build --config Debug --target RtPbrSurvey
python Tests/PathTracing/validate_nrd_runtime_reconstruction.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_runtime_reconstruction.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <another-new-directory> --disabled-distance-diagnostics
```

### Reconstruction Resource Lifecycle: Step 4 (2026-10-16)

The existing 19-stage PT history automation now accepts -DebugPreviewResource
as its capture target and records the resource name at every checkpoint. Its
default target remains PathTracing.Accumulation. The new
Tests/PathTracing/validate_nrd_runtime_history.py runs that timeline separately
for NrdReconstructedSpecular, NrdReconstructedDiffuse and NrdTiles, validating
the exact ordered case set, CPU state, resource identity, native format,
dimensions, sample index, seed and primary-sampling metadata before assessment.
The report retains per-resource commands, capture hashes and checkpoint records,
including failed assessments, and keeps inputReady false.

Pause must retain bit-identical buffers. Reset while paused must clear them.
Unsupported multi-sample batches must clear conversion/reconstruction output.
Returning to one sample per frame must reproduce a fresh deterministic result.
Camera, lighting, material, instance transform and resize mutations must match
their respective manually reset references. Non-tile outputs must retain a
positive radiance signal; non-resize edits must also differ from their previous
state, preventing stale but repeatable buffers from passing. Tiles must remain
zero and use ceil(renderSize/16) dimensions.

The first run failed because the existing geometry mutation changed world._41.
Instance matrices are stored transposed, so X translation belongs in world._14;
the old mutation made the matrix non-affine and the geometry snapshot correctly
rejected it with host gate 9. The timeline now changes _14. The failed run and
the diagnostic timeline are retained in nrd-runtime-history-20261016 and
nrd-runtime-history-diagnostic-20261016; no exclusion gate was weakened.

RTX 2080 Ti validation in nrd-runtime-history-fixed-20261016 passed all 57
checkpoints (19 per resource) with eight bit-exact comparisons per resource.
The initial render size was 1920x1061 and resized resources were 1280x720;
the corresponding tile sizes were 120x67 and 80x45. All three runtime logs
contained zero D3D12 errors. The existing two buffer initial-state warnings
remain. Debug x64 build passed with zero compiler warnings/errors. Sixteen
runtime Python tests, fifteen existing history unit tests, and five focused
shader/SDK CTest tests passed. This is automated resource-lifecycle evidence on
one scene/GPU; full RELAX temporal history, denoising quality and interactive
GUI acceptance remain separate work.

The default PathTracing.Accumulation timeline also passed all 19 checkpoints in
nrd-runtime-default-history-20261016 after the translation fix. Its mutation
signals remained observable and each changed state matched its fresh reference.

A diagnostic launch also exposed a log-open failure: its missing parent folder
left m_logFile null, and the history runner called fprintf on it, producing the
CRT output.cpp stream assertion. -LogToFile now creates parent directories,
closes a file descriptor if _fdopen fails, and emits a debugger diagnostic if
opening fails. The history runner exits with code 1 when no log is available;
other timeline failures also return code 1. A directory supplied as the log
filename was exercised in nrd-runtime-log-failure-20261016 and returned 1
without an assertion dialog.
A separate launch in nrd-runtime-log-parent-20261016 started with no output
folder, created logs/runtime.log automatically, captured nonzero reconstruction
radiance and exited with code 0 without D3D12 errors.

```powershell
python Tests/PathTracing/validate_nrd_runtime_history.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python -m unittest discover -s Tests/PathTracing -p test_nrd_runtime_*.py
python -m unittest discover -s Tests/PathTracing -p test_history.py
```

### RELAX Input Admission And SDK Planning: Step 5A (2026-10-16)

Steps 3-4 and the log-open assertion fix were committed as d77be08. Step 5A adds
PT NRD Reconstruction Input Admission after the live reconstruction pass, under
the same default-off reconstruction option. The shared production HLSL helper
validates both lobes and admits or excludes the entire pixel. It preserves raw
reconstruction outputs and writes four separate RGBA32F Debug Preview textures:

- PathTracing.NrdInputDiffuse and NrdInputSpecular: admitted demodulated signals.
- PathTracing.NrdInputViewZ: valid linear depth, or 1e6 for excluded pixels.
- PathTracing.NrdInputAdmission: R = local admission, G = diagnostic code,
  B/A = admitted inactive diffuse/specular lobe flags.

Codes are 0 for admission, 1 for excluded/out-of-range pixels, 2 for invalid
guides/masks, 3 for invalid signal/distance-state data and 4 for unresolved active
distance. Inactive lobes require canonical state 4 and zero RGB, then all four
signal components become zero, removing irrelevant neighbor distances. Active
zero distances reject the whole pixel. Finite nonnegative values are required;
the existing reconstruction distance ceiling tolerance of 65504.5 is retained
for interpolation overshoot, with admitted distances capped to 65504. Admission
is not full backend readiness: camera/MV and temporal history still need their
own contract. Full RELAX support remains unavailable and Native stays final.

The graph declares all six SRV inputs and four UAV outputs. The normal resource
allocation, history-clear, resize and Debug Preview paths cover these textures.
Every admission thread writes all outputs, including exclusion defaults. Pause
retains the latest results. The reusable 19-stage history validator accepts the
new diffuse/specular resources through repeatable --resource arguments.

The default-off RTPBRSURVEY_NRD_SDK_DIAGNOSTICS option builds the exact pinned
SDK CPU scheduler from unmodified upstream sources. It uses MathLib v11 and the
ShaderBlob.h types from ShaderMake commit 18f5a344e7ca8fa65daaf079d07bc8ce38453e05.
CMake verifies the dependency commits and clean SDK/header sources. It does not
run upstream CMake in the source checkout, download compilers, add NRI or embed
SDK GPU bytecode; the generated config resolves to the pinned contract config.

Renderer/NrdRelaxSdkAdapter.h is SDK-neutral; NRD headers/types stay in its cpp.
The PIMPL owns the real RELAX_DIFFUSE_SPECULAR instance and calls CreateInstance,
SetDenoiserSettings, SetCommonSettings and GetComputeDispatches. External
reconstruction is already done, so SDK hit-distance reconstruction is OFF.
Dispatch names, resource bindings and constant bytes are deep-copied before a
subsequent SDK query can overwrite them. The frame descriptor requires finite,
nonsingular non-jittered column-major matrices and 16-bit-compatible dimensions.
Its proposed screen-space MV scale is (0.5, -0.5, 0) for previous-minus-current
NDC; application camera/MV wiring is still pending. Reset requests
CLEAR_AND_RESTART. The adapter is exercised by the SDK test executable and is
not yet invoked by the rendering loop. Descriptor counts are CPU planning
diagnostics, not a guarantee about a future bytecode-embedded pipeline layout.

SDK tests passed creation/reinitialization/shutdown, reset versus continuation,
resize grids, invalid frame rejection and owned dispatch lifetime after later
queries/shutdown. The SDK reports 27 pipelines, ten permanent textures, seven
transient textures and 29 reset dispatches in this CPU-only configuration.
The app Debug x64 build passed with zero compiler warnings/errors; the SDK test
link had the existing unused sl.interposer delay-load warning. Six focused
CTest tests and nineteen runtime Python tests passed.

GPU evidence on RTX 2080 Ti:

- nrd-runtime-admission-20261016: ten matched captures, first material made fully
  metallic in a copied fixture, bit-exact CPU comparison. 1,286,774 pixels were
  admitted; 1,089,008 had inactive diffuse lobes; 442 nonzero inactive neighbor
  distances were zeroed. The remaining 750,346 pixels were excluded.
- nrd-runtime-admission-disabled-20261016: ten matched captures with distance
  diagnostics disabled; all 2,037,120 pixels excluded, zero admitted signals.
- nrd-admission-vectors-20261016: sixteen explicit success/failure vectors ran
  the shared production helper on WARP and hardware. Both matched independent
  CPU outcomes bit for bit, including NaN, negative radiance, contradictory
  distance states, inactive RGB, unresolved distances and the packing ceiling.
- nrd-admission-history-final-20261016: 38 checkpoints for NrdInputSpecular and
  NrdInputDiffuse, including pause retention, reset clear, incompatible sampling,
  all five edit/resize cases and eight bit-exact replay comparisons per resource.
  The final report also records conversion/admission shader and helper hashes;
  the preceding nrd-admission-history-20261016 run passed the same timeline.

The two ten-capture cohorts preceded extracting the admission helper into the
shared HLSL include. The final shared helper is covered by the vector tests and
the 38-checkpoint runtime lifecycle run. Each report records its actual binary
and shader hashes rather than treating these executions as one binary version.

All capture/harness logs contained zero D3D12 errors. These tests establish local
input sanitation, not full RELAX denoising quality. Next is D3D12 pipeline/pool/
descriptor execution of the SDK dispatches, followed by camera/MV and temporal
history validation and separate denoised scene-color composition.

```powershell
git clone https://github.com/NVIDIA-RTX/ShaderMake.git build/_deps/nrd-shadermake-src
git -C build/_deps/nrd-shadermake-src checkout --detach 18f5a344e7ca8fa65daaf079d07bc8ce38453e05
cmake -S . -B build -DRTPBRSURVEY_BUILD_APP=ON -DRTPBRSURVEY_BUILD_TESTS=ON -DRTPBRSURVEY_TEST_NRD_PACKING=ON -DRTPBRSURVEY_NRD_CONVERSION_DIAGNOSTICS=ON -DRTPBRSURVEY_TEST_NRD_RECONSTRUCTION=ON -DRTPBRSURVEY_NRD_RECONSTRUCTION_DIAGNOSTICS=ON -DRTPBRSURVEY_NRD_SDK_DIAGNOSTICS=ON
cmake --build build --config Debug --target RtPbrSurvey.NrdSdkAdapterTests RtPbrSurvey.NrdAdmissionVectors RtPbrSurvey
ctest --test-dir build -C Debug -R NrdSdkAdapter --output-on-failure
python Tests/PathTracing/validate_nrd_admission_vectors.py --output <new-directory>
python Tests/PathTracing/validate_nrd_runtime_admission.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_runtime_admission.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <another-new-directory> --disabled-distance-diagnostics
python Tests/PathTracing/validate_nrd_runtime_history.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --resource NrdInputSpecular --resource NrdInputDiffuse
```

### Native D3D12 RELAX Dispatch: Step 5B (2026-10-16)

The default-off RTPBRSURVEY_NRD_SDK_DIAGNOSTICS option now runs the SDK's actual
RELAX_DIFFUSE_SPECULAR dispatch list after reconstruction input admission.
NrdRelaxSdkAdapter keeps SDK headers private and copies pipeline identifiers,
resource ranges, pool format/downsample metadata, bindings and constant bytes.
NrdRelaxD3D12 owns D3D12 pipelines, private permanent/transient textures and
per-frame descriptors/upload buffers. No NRI dependency is introduced.

The build-time NrdShaderManifest tool links the same pinned SDK scheduler and
exports its 27 shaderIdentifier strings. compile_nrd_shaders.py compiles those
exact SDK shader permutations with the pinned encoding configuration and local
DXC. Generated bytecode and manifests stay in build outputs. At initialization,
the adapter compares every identifier to the runtime SDK list before creating
PSOs. Both manifest and bytecode outputs are tracked as build dependencies.

The graph adds PT NRD RELAX GPU Diagnostics. Its external SRV inputs are MotionVectors,
NrdNormalRoughness, NrdInputViewZ, NrdInputDiffuse and NrdInputSpecular. The two
RGBA32F UAV outputs are PathTracing.NrdOutputDiffuse and PathTracing.NrdOutputSpecular,
registered for raw captures and Debug Preview. Outputs are cleared before each
evaluation so excluded pixels cannot expose stale values. Private SDK pools use
SDK-declared formats and ceil-divided dimensions. The adapter transitions pool
and external resources per dispatch, inserts UAV barriers, and restores external
states to the graph contract. The main descriptor heap is rebound afterward.

Descriptors and constant bytes have separate storage for every engine frame slot;
the existing frame fence protects reuse. A resize flushes GPU work before releasing
old private textures. A new allocation requires a reset, and CLEAR_AND_RESTART
generates SDK clear dispatches for the pools. PT invalidation resets the NRD frame
index and history. Pause records no NRD dispatch; camera/light/material/geometry
edits restart history conservatively. Current/previous non-jittered camera matrices
and resource dimensions are passed explicitly. NDC previous-minus-current motion
uses normalized screen-space scale (0.5, -0.5, 0).

RTX 2080 Ti evidence:

- nrd-relax-gpu-dispatch-20261016: nine captures covering two inputs, admission,
  both denoised outputs, bit-exact fresh-process repeats for both outputs, and
  zero output with distance diagnostics disabled. Both lobes had 1,286,774
  admitted/changed pixels. All values were finite/nonnegative and excluded pixels
  stayed zero. This confirms actual filtering, not a denoising quality threshold.
- nrd-relax-history-20261016: both outputs passed all 19 existing checkpoints,
  including pause/reset, incompatible sampling, five scene edit/resize cases and
  eight exact replay comparisons per resource. Reports record executable and all
  27 SDK shader hashes. The original report's generic limitation text predates
  this RELAX resource extension; camera edits in this test reset NRD history.
- nrd-relax-history-final-20261016: the final formatted Debug executable repeated
  all 38 checkpoints successfully, with zero D3D12 errors and updated limitation
  text. The nine-capture cohort above used the preceding executable; both reports
  retain their actual binary/shader hashes.
- Debug x64 app and SDK layout-test builds succeeded with no warnings/errors.
  Capture logs contained zero D3D12 errors; the two known initial-buffer-state
  warnings remain. Focused numeric tests reject nonfinite/negative outputs,
  passthrough results, excluded-pixel writes and shape mismatches.
  All 56 NRD Python tests and six focused CTest tests passed. The first sandboxed
  Python run hit five temporary-directory permission errors; the unrestricted
  rerun passed all tests. This was an execution-environment failure, not a numeric
  acceptance failure. The final SDK layout test was relinked and passed again.

This completes GPU dispatch diagnostics, not full backend availability. Native
scene-color output and the existing NotIntegrated support gate remain until
material weighting restoration, stable/background composition, continuous-motion
reprojection, fallback and end-to-end numeric/visual acceptance are completed.
The current GUI can inspect the two NRD output resources but does not yet select
them as the final PT image.

```powershell
python Tests/PathTracing/validate_nrd_gpu_dispatch.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_runtime_history.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --resource NrdOutputSpecular --resource NrdOutputDiffuse
```

### Continuous Camera History Diagnostics: Step 6A (2026-10-17)

The diagnostic-only -NrdContinuousCameraHistory flag enables an experimental
camera-pose continuation policy. It requires RTPBRSURVEY_NRD_SDK_DIAGNOSTICS.
Ordinary GUI camera edits keep the default reset behavior. Native PT accumulation
still resets on every camera change; only the private NRD temporal pools can be
retained. No final denoiser selection or default-on policy is added.

Continuation requires unchanged projection type, FOV, lens shift, near/far planes
and orthographic extent. Scene, material, lighting, resize, settings and manual
reset invalidations still reset NRD. Before dispatch, the previous SDK camera
matrices are compared to the native MV previous-view-projection basis. If a
skipped evaluation or paused camera move has changed that basis, NRD restarts.
This comparison uses a 2e-6 relative/absolute matrix arithmetic allowance. It is
not an automatic large-camera-cut detector; explicit resets remain appropriate
for cuts while using this experimental CLI mode.

Every raw diagnostic capture now includes nrdLastEvaluation: actual NRD frame
index, reset flag, dispatch count, current/previous camera matrices and screen MV
scale. The metadata describes the last evaluation, which can precede a paused
capture. The SDK and metadata share the same (0.5, -0.5, 0) scale constant.

Evidence on RTX 2080 Ti:

- nrd-camera-motion-20261017 retained the first failed static-MV pilot. Its
  exact-zero assertion was too strict: observed maximum static motion was
  5.96e-7 NDC with identical current/previous camera matrices. The numeric MV
  comparison had already passed the existing format-derived contract.
- nrd-camera-motion-qualified-20261017 passed twelve captures: static, +4 degree
  orbit, -4 degree orbit and +4 degree per-frame-reset control, each for native
  MotionVectors and both NRD outputs. Each moving trajectory spans twenty frames
  and is captured after thirty warm-up frames. The continued SDK frame index was
  30 with reset=false; the moving reset control used frame index 0/reset=true.
- Independent finite-plane intersections and matrix projection matched native
  RG16 motion within the pre-existing bound, 2 ULP(expected half) + 3e-6 NDC.
  Maximum normalized errors were 0.153 static, 0.379 positive, 0.394 negative;
  no threshold was expanded for the pilot. The SDK UV scale/sign and both SDK
  camera matrix pairs also matched the actual rendering/MV basis.
- All captures in each case had matched camera/sample metadata. The positive
  and reset-control native inputs matched too. Continued and reset outputs
  differed at 1,286,510 diffuse and 1,286,515 specular RGB pixels, establishing a
  measurable reset-policy effect rather than a claimed quality improvement.
  This control also resets SDK frame index (and its sampling rotators), so the
  RGB difference is not an isolated temporal-reuse measurement. The executed
  report's historyEffect field must be interpreted with this qualification;
  future reports include it in their limitation text as well.
- All logs had zero D3D12 errors. The Debug app build succeeded without warnings.
  The SDK policy test build had only the existing unused interposer delay-load
  warning. The SDK test covers seven projection changes, matching/stale/nonfinite
  MV bases; all 63 NRD Python tests and six focused CTest tests passed.
- nrd-camera-default-history-20261017: the final formatted Debug executable, with
  continuous camera history disabled, passed all 38 existing pause/reset/edit/
  resize checkpoints for the two NRD outputs. D3D12 errors remained zero.

The capture report records the executed binary, test script and all SDK shader
hashes. Subsequent source formatting/EOL normalization does not replace those
hashes. This is a small static-geometry camera-orbit qualification, not acceptance
of moving objects, arbitrary cuts, disocclusion/ghosting quality, variable frame
time, jitter or final-image composition. Those remain separate integration and
visual gates. Next is material-weight restoration and stable/background/native
fallback composition into a separately inspectable denoised scene-color output.

```powershell
python Tests/PathTracing/validate_nrd_camera_motion.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
```

### Material Restoration And Diagnostic Composition: Step 7A (2026-10-17)

The conversion pass now stores the SAME NRD_MaterialFactors results used for
demodulation in RGBA32F NrdDiffuseFactor and NrdSpecularFactor textures. Alpha is
one only when conversion succeeds; every other pixel is cleared. Existing
conversion bindings remain unchanged with SDK diagnostics disabled. With SDK
diagnostics enabled, two additional UAV ranges bind these persistent textures.

PT NRD Material Restore and Composition runs after RELAX. It writes independently
inspectable PathTracing.NrdSceneColor and NrdCompositionDiagnostics resources:

```text
color = ((NRD.diffuse.rgb * storedDiffuseFactor.rgb
        + NRD.specular.rgb * storedSpecularFactor.rgb)
        + StableRadiance.rgb) + BackgroundRadiance.rgb
```

Inactive lobe flags force that lobe to zero before addition, even if spatial
filtering imported nonzero neighbor radiance. Original material factors are not
recomputed from filtered normals or roughness. In the pinned SDK,
RELAX_BackEnd_UnpackRadiance is the identity, so the RGB channels are already the
linear radiance to remodulate. Stable/background terms are added exactly once.

Unadmitted pixels, invalid masks/factors/signals and arithmetic overflow select
the existing Native SceneColor for the whole pixel. Diagnostics.x is one only
when NRD composition is used; y is 0 used, 1 excluded, 2 invalid mask, 3 invalid
factors, 4 invalid radiance, 5 overflow, 6 invalid Native input. Invalid Native
input itself produces explicit diagnostic code 6 and safe opaque black. zw are
inactive-lobe flags only on successful composition. Native SceneColor is RGBA16F;
the new result is RGBA32F, so fallback preserves decoded half values exactly,
not identical byte layout. Existing Native FP16/HDR limits are unchanged.

The new buffers join the existing render-size allocation, history clear, resize,
RenderGraph and Debug Preview paths. Native SceneColor also gains raw .ptbuf
capture support so fallback can be checked directly. ToneMap still consumes
Native SceneColor; QueryNrdRelaxSupport remains gated. Final-image selection and
global SDK-failure recovery remain separate work units.

Evidence on RTX 2080 Ti:

- nrd-composition-qualified-20261017: fifteen matched captures. 1,286,774 pixels
  used NRD composition, 750,346 used exact Native fallback; 1,089,008 admitted
  pixels had inactive diffuse lobes. CPU float32 composition matched with zero
  error under the predefined 8 ULP(reference float32) + 1e-6 bound. Stored-factor
  conversion round-trip errors were at most 6.26e-8 diffuse / 1.19e-7 specular.
  Background was nonzero at 750,346 pixels; stable radiance was zero in this
  runtime fixture, so primary emissive/unlit live coverage is still pending.
- nrd-composition-disabled-20261017: six captures with distance diagnostics off;
  all 2,037,120 pixels selected exact Native fallback, and both factors cleared.
- nrd-composition-vectors-final-20261017: seventeen shared-production-helper
  vectors, bit-exact on WARP and hardware. Cases include nonzero stable and
  background, inactive neighbor leakage, invalid masks/factors, NaN/negative
  signals, overflow, invalid Native and HDR-preserving helper fallback.
- nrd-composition-history-20261017: all nineteen checkpoints passed, including
  Pause/Reset, settings changes, camera/light/material/geometry edits and resize.
  Nine exact replay pairs include non-accumulated Native fallback. Reset while
  paused clears all channels; every evaluated composed output has alpha one.
- Debug app, SDK-check and vector builds passed without warnings/errors.
  All 67 NRD Python tests and six focused CTest tests passed. Capture and GPU
  harness logs contained zero D3D12 errors; known initial-buffer warnings remain.

Failed pilots are retained: nrd-composition-live-20261017 stopped because Native
SceneColor did not yet support raw capture; nrd-composition-final-20261017 stopped
because its validator assumed RGBA32F for the Native RGBA16F resource. The final
validator distinguishes those formats. No numeric acceptance threshold changed.
Reports retain actual executable/shader/helper/script and captured-buffer hashes.

This qualifies diagnostic arithmetic and resource lifetime, not denoising quality
or shipping backend availability. Next: explicit final-image selection, live
emissive/unlit scenes, global failure fallback, then user-assisted Native/NRD A-B
review for ghosting, edges, fine highlights and moving disocclusions.

```powershell
cmake --build build --config Debug --target RtPbrSurvey.NrdCompositionVectors RtPbrSurvey
python Tests/PathTracing/validate_nrd_composition_vectors.py --output <new-directory>
python Tests/PathTracing/validate_nrd_composition.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_composition.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --disabled-distance-diagnostics
python Tests/PathTracing/validate_nrd_runtime_history.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --resource NrdSceneColor
```

### Diagnostic Final-Image Selection: Step 7B (2026-10-17)

PathTracingSettings.nrdPreview is an explicit, persisted opt-in, default false.
The normal Debug UI and Scene Editor renderer UI expose NRD RELAX Preview
(Diagnostic), Pixel Center Primary, Denoiser Distance Diagnostics and the current
PT Presentation status. The CLI equivalent is -NrdPreview; it enables PT but does
not silently change estimator/sampling settings or claim production readiness.
QueryNrdRelaxSupport remains unavailable pending the remaining acceptance gates.

To inspect NRD in the main viewport, choose Path Tracing / Radiance, one sample
per frame, Pixel Center Primary and Denoiser Distance Diagnostics, then enable
the preview. This requires the existing three NRD diagnostic CMake options and
their packing/reconstruction dependencies. Normal builds retain Native PT.

ToneMap dynamically binds NrdSceneColor only after current composition has been
recorded. The graph declares both Native and NRD candidate reads when requested,
because composition validity changes during graph execution. Both sources pass
through the same ToneMap/exposure/transfer function. Per-pixel Native fallback
remains in the composition shader; the presentation selector adds whole-frame
Native fallback for missing diagnostic builds/backends, incompatible inputs and
an output awaiting evaluation. UI, capture logs and raw-buffer metadata report
the requested and effective presentation separately.

Composition validity clears on PT invalidation (including preserved-NRD camera
history), sample-batch changes, history clears and before tracing a new frame.
Only successful recording of a current composition after nonzero SDK dispatches
marks it valid again. Pause retains an unchanged valid result; Reset while paused
selects Native instead of a stale denoised image. Toggling only nrdPreview does
not invalidate either PT accumulation or current NRD composition. Changes to
sample batch size still retain Native accumulation as required by the existing
history contract, but reset private NRD history and cannot resurrect a stale NRD
output while paused.

Validation uses final-output PNGs for source A/B and raw composed buffers for the
19-stage lifecycle. It does not treat a changed PNG as a quality improvement.
The final-output comparator uses a predefined central ROI, avoiding UI/timing
overlays; incompatible inputs require exact pixel equality with their matched
Native run, not a relaxed numerical threshold.

Global SDK initialization/recording exceptions and device loss are NOT recovered
by this selector. It does not catch partial command-list recording failures or
silently continue with uncertain GPU state. That remains a separate work unit,
along with live emissive/unlit coverage and user-assisted denoising-quality A/B.

Evidence on RTX 2080 Ti (generated reports remain under ignored bin):

- nrd-presentation-qualified-20261017: ten final-output captures across five
  Native/preview pairs. Qualified preview changed 243,610 pixels in the fixed
  ROI (768,265)-(1248,795). Distance diagnostics off, random primary sampling,
  four samples/frame and Normal output each produced exact Native fallback.
- nrd-presentation-sdk-off-20261017: two final-output captures from an independent
  diagnostic-OFF Debug build. Requesting NRD reported the not-built fallback and
  exactly matched the Native ROI. The normal build has no NRD runtime dependency.
- nrd-presentation-history-qualified-20261017: nineteen raw captures and nine
  exact replay pairs passed on the final diagnostic executable. Fifteen capture
  checkpoints selected NRD, three incompatible sample-batch checkpoints selected
  Native and reset-paused selected the awaiting-evaluation Native fallback. The
  paused display-only off/on test retained sample counts, index and valid output.
- Seventy NRD Python tests and nine focused CTest tests passed, including all
  thirty-two presentation-gate combinations, settings round-trip and CLI parsing.
  Both application Debug builds completed with zero warnings/errors. Two test
  targets retained the existing unused Streamline DELAYLOAD linker warning.
- All capture logs contained zero D3D12 errors; the two existing initial-buffer
  state warnings remain. Reports preserve executable, shader, script, preset,
  scene and capture hashes. Earlier presentation/history pilot reports are kept
  separately rather than overwritten. A test-target post-build D3D12 DLL copy
  initially collided with the running capture application; its retry after app
  exit passed without a source workaround.

```powershell
python Tests/PathTracing/validate_nrd_presentation.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
python Tests/PathTracing/validate_nrd_presentation.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory> --exe build/nrd-off/Debug/RtPbrSurvey.exe --without-sdk
python Tests/PathTracing/validate_nrd_runtime_history.py --source bin/PathTracingValidation/nrd-composition-qualified-20261017 --output <new-directory> --resource NrdSceneColor --nrd-preview
```

### Pre-Recording Failure Policy: Step 7C (2026-10-17)

NRD initialization is optional in SDK diagnostic builds. Failure to load/validate
the manifest, create optional pipelines or allocate backend storage now disables
the NRD path while retaining Native PT, provided the D3D12 device is healthy.
The initialization guard covers conversion/reconstruction/admission/composition
pipeline creation as well as the isolated SDK backend. Native renderer pipeline
creation is outside that guard and retains its existing fatal error behavior.

The D3D12 backend Record operation now has two phases:

1. Prepare the entire frame without backend command-list calls: validate external
   resource shape/aliasing, obtain all SDK dispatches, allocate resolution pools,
   validate every pipeline/binding/grid/constant range, fill the fenced frame
   slot's private descriptors/constants and precompute transitions/final states.
2. Record the already prepared barriers/bindings/dispatches, restore external
   states and publish private pool states. This phase performs no allocations,
   checked lookups, SDK scheduling or descriptor creation and is outside the
   recoverable exception handler.

Resolution allocation is transactional: permanent and transient temporary pools
are published only after both allocations succeed. Planned state changes do not
mutate actual pool tracking before recording. A NrdPreparationError guarantees
that this backend recorded no commands. Engine-owned graph transitions and output
clears may already be recorded, but their states remain valid for Native fallback.
Only that typed error is caught around runtime evaluation; other exceptions escape.

Healthy-device preparation failure latches the backend disabled for the engine
lifetime. It does not retry on settings changes, PT Reset or resize. Already
submitted frames may still reference its resources, so the backend is NOT freed
in the failure handler. Existing fence-protected resize/shutdown paths retire
those resources safely. PT accumulation is not invalidated by NRD failure.

On the failing frame, the already scheduled composition uses an explicit
forceNative constant and writes exact decoded valid Native color with excluded-pixel
diagnostics (invalid Native retains the existing safe-black diagnostic policy).
Its validity stays false; ToneMap selects Native. Later graphs skip
RELAX/composition altogether. UI and PNG/raw capture diagnostics expose failure
stage/reason, prepared/planned dispatch counts and failure count. Raw metadata
distinguishes an attempted evaluation from successfully recorded GPU commands,
so an earlier success is never labeled as the failed frame's evaluation.

DXGI DEVICE_REMOVED, DEVICE_HUNG, DEVICE_RESET and DRIVER_INTERNAL_ERROR results
are explicitly fatal even if GetDeviceRemovedReason has not updated yet. A failed
device-health query is also fatal. No exception after recording starts is caught
as a preparation failure. This is NOT general D3D12/device-loss recovery; Native
cannot safely continue on a lost device. Actual device removal/hang is not injected
by these tests, and no driver, system setting or SDK artifact is changed.

The diagnostic-only CLI -NrdDiagnosticFailure accepts initialization,
last-dispatch or after-four-frames. Initialization points the adapter at a missing
manifest directory. The latter modes invalidate ONLY the final SDK dispatch's
pipeline index, either immediately or after four successful frames. Thus the test
must prepare all earlier dispatches before rejecting the final one; catching a
failure after partially dispatching cannot satisfy this contract. These controls
are never persisted in presets and are rejected by SDK diagnostic-OFF builds.

Evidence on RTX 2080 Ti:

- nrd-failure-fallback-20261017: thirteen single captures (ten final-output PNGs,
  three raw buffers) and a separate nineteen-checkpoint Native accumulation
  timeline passed. Missing-manifest initialization, immediate last-dispatch
  rejection, the failure frame after four successful frames and later disabled
  operation each produced exact Native PNGs in the fixed comparison ROI.
- Before injection at sample three, NRD remained active and changed 248,572 ROI
  pixels. Immediate failure prepared 28 of 29 dispatches; failure after four
  successful frames prepared 9 of 10. Both rejected the final dispatch before
  backend command recording. Failure count stayed exactly one through subsequent
  frames, Reset, settings, camera/light/material/geometry edits and resize.
- On the failing sample-five frame, all 2,037,120 composed pixels exactly matched
  decoded Native SceneColor. Every composition diagnostic was (0,1,0,0). Raw
  metadata reported the attempted frame index four, recorded=false and zero
  recorded dispatches, rather than a stale successful evaluation.
- The failed-backend Native timeline retained four exact replay pairs and five
  exact changed-state/fresh pairs. Batch accumulation relative RMSE was
  2.09e-7 under the unchanged 1e-6 bound. D3D12 error count was zero.
- nrd-failure-normal-history-20261017: normal NRD, without injection, passed all
  nineteen checkpoints and nine exact replay pairs after the preparation split.
- nrd-failure-normal-presentation-20261017: ten final-output A/B captures passed;
  qualified NRD changed the same 243,610 ROI pixels and all four incompatible
  cases retained exact Native fallback. An additional full-frame comparison in
  nrd-failure-previous-output-comparison-20261017 found all ten PNG files
  hash-identical to the historical Step 7B captures. This explicitly separates
  the current execution from the historical reference rather than relabeling it.
- nrd-failure-sdk-off-20261017: two final-output captures passed with SDK
  diagnostics disabled; NRD opt-in reported not-built and matched Native exactly.
- Seventy-three NRD Python tests and nine focused CTest tests passed. The latter
  include fatal/nonfatal HRESULT policy and missing/invalid CLI-value rejection.
  Application Debug builds with SDK diagnostics ON and OFF passed with zero
  warnings/errors. Reports preserve the executed binary/source/shader/script,
  fixture and capture hashes. Driver/device-loss injection is explicitly excluded.

```powershell
python Tests/PathTracing/validate_nrd_failure_fallback.py --source bin/PathTracingValidation/nrd-step2s-material-cohort-20261013 --output <new-directory>
```

### Step 8A: Make opaque Unlit input reachable

Live stable-radiance validation exposed an unreachable input: shaders already
handled the Unlit flag, but glTF import did not populate it. The importer now
recognizes KHR_materials_unlit and preserves the flag through both flattened and
node/mesh SceneBuilder paths. Ordinary materials remain lit by default.

Unlit uses base color without lighting or emission. GPU material creation and
runtime material updates keep its emissive scale zero, and the CPU emissive
triangle table excludes Unlit. The existing opaque, single-sided importer
limitations remain unchanged; this is not full glTF material conformance.
The color policy follows the
[Khronos Unlit specification](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_unlit/README.md).

This prerequisite does not qualify NRD on Unlit. Dedicated live captures must
still verify primary emissive composition, Unlit Native fallback and background
preservation. No GPU evidence from earlier steps is relabeled as this validation.

Validation: Debug application build passed. GltfNodeMesh and EmissiveTriangle
tests passed, covering required/optional Unlit import, both SceneBuilder paths,
ordinary material defaults, unsupported-extension rejection and emitter exclusion.
The existing seventy-three NRD Python tests also passed. Test-only links retain
the known LNK4199 unused Streamline delay-load warning.

### Step 8B: Live stable-radiance composition contract

The persistent fixture Assets/Scene/PathTracingValidation/13-nrd-stable-radiance
places a lit emissive panel, an opaque Unlit panel and an ordinary lit panel in
one plane. Unlit deliberately declares nonzero emission, which must be ignored.
Primary misses provide nonzero background coverage. The stored scene contains
Japanese descriptions of the intended checks and limits.

Unlit primary hits retain a valid dedicated primary position, but dedicated PBR
distance/direction samples stay uncollected. Conversion and admission must reject
them, and composition must use exact decoded Native color. This avoids importing
neighboring PBR illumination into an unsupported Unlit signal. Native PT tracing
and accumulation are unchanged. The other two panels must actually enter NRD
composition; an all-Native result cannot pass this fixture.

The new runner captures nineteen resources with identical deterministic-frame
metadata, requires successful NRD recording/presentation, checks all four spatial
cohorts, and applies the existing independent CPU remodulation/composition oracle.
Stable-radiance RGB must match material emission or Unlit base color within 1e-6.
Composition retains 8 ULP(float32) plus 1e-6; Unlit/background fallback remains
exact decoded Native. Required resource identities, formats and coverage are
checked, not inferred from capture count.

Four new Python tests reject missing resources, insufficient coverage, missing or
doubled emission, Unlit contamination/admission, invalid hit state, background
loss/leakage, composition corruption and incomplete/mismatched metadata. A C++
SceneDocumentBuilder test loads the actual asset and checks all three panels and
their Unlit flags. The first live pilot failed before capture because the new
fixture omitted mandatory orthographicHeight; its failed report is retained in
bin/PathTracingValidation/nrd-stable-radiance-20261017. The corrected fixture is
tested separately; no failed pilot is counted as GPU evidence. Temporary startup
diagnostic code was removed before the corrected run.

```powershell
python Tests/PathTracing/validate_nrd_stable_radiance.py --output <new-empty-directory>
```

This is a fixed-camera raw-input/composition contract, not final ToneMap quality,
general Unlit glTF conformance, disocclusion, animation or temporal acceptance.

Corrected live evidence: bin/PathTracingValidation/nrd-stable-radiance-r2-20261017
passed all nineteen captures with zero D3D12 errors. Each panel covered 96,866
pixels. All emissive and ordinary pixels (193,732 total) used NRD composition;
all 96,866 Unlit pixels and 1,746,522 background pixels matched decoded Native
exactly. Stable RGB contained only zero, the declared lit emission and the Unlit
base color. Maximum composition error was 5.9604645e-8 (0.04036 of the unchanged
bound). Factor round-trip maximum errors were 1.910809e-8 diffuse and 2.397293e-9
specular. The report records fixture, executable, shader, runner and capture
hashes plus common runtime metadata. All seventy-seven NRD Python tests and the
SceneDocumentBuilder test passed. Debug application build passed with zero
warnings/errors; the test-only link retains the known LNK4199 delay-load warning.

### Step 9A: Disocclusion Visibility And History Measurement

Assets/Scene/PathTracingValidation/14-nrd-disocclusion contains two static opaque
planes: a red foreground occluder and a larger green background. Its Japanese
description identifies the silhouette/revealed region to inspect. The diagnostic
runner captures Albedo, MotionVectors, NrdSceneColor and Native SceneColor for a
static camera, +/-20 degree orbits and a +20 degree per-frame-reset control.
All runs use thirty warm-up frames, with motion in the final twenty frames.

The visibility oracle independently intersects finite planes at a four-pixel
sampling stride. It projects each current world-space first hit into the previous
view and traces the previous camera ray to determine whether the foreground hid
that background point. Both views' plane-edge margins and previous out-of-frustum
points are excluded. Moving runs require at least twenty revealed samples and
all runs require at least one hundred retained-background/foreground samples.
Static camera reprojection must reveal no previously occluded background.

Primary Albedo must match the independent first-hit material, accounting for the
actual SceneDocument linear-to-rounded-sRGB8-to-linear input path. The comparison
uses 2 ULP(decoded half) + 3e-6; it does not compare directly to unquantized JSON.
The initial pilot stopped on that incorrect direct-color expectation and remains
in bin/PathTracingValidation/nrd-disocclusion-20261017 as failed evidence. Its
captures are not included in the corrected qualification.

Resource identity/format, common runtime metadata, successful NRD recording and
presentation, SDK matrix/reset/frame state and the existing independently
projected half-format MV bound are mandatory. Positive/reset-control runs must
have identical Native camera/sample inputs. Reset control also restarts the SDK
frame index, so its output is not an isolated temporal-reuse comparison.

The report measures RGB means and differences from same-frame noisy Native in
revealed and retained regions. These are NOT quality acceptance criteria:
qualityVerdict stays not-evaluated even when geometry/MV/history passes. A test
with deliberately red-contaminated finite output verifies that measurement is
not mislabeled as quality acceptance. Other new tests cover static and both
visibility directions, false motion, wrong materials, nonfinite colors,
missing/wrong capture state and the document color quantization reference.
The actual saved scene also passes SceneDocumentBuilder schema/geometry loading.

```powershell
python Tests/PathTracing/validate_nrd_disocclusion.py --output <new-empty-directory>
```

This step does not accept visual ghosting, converged-radiance error, motion
reversal/settling, moving objects, camera cuts or animation. Ordinary GUI camera
edits still reset NRD; continuation remains an explicit diagnostic CLI policy.

Corrected evidence: bin/PathTracingValidation/nrd-disocclusion-r2-20261017 passed
all sixteen captures with zero D3D12 errors and matching per-case runtime inputs.
Each orbit direction exposed 252 independently identified sampled background
points; the static case exposed zero. Continued cases retained NRD frame index
30/reset=false, while the matching +20 degree reset control used index 0/reset=true.
The report retains actual color means separately from its geometry/history gate,
with qualityVerdict=not-evaluated. All eighty-two NRD Python tests and the saved
scene's SceneDocumentBuilder test passed; the Debug test build had zero warnings
and errors. No renderer/shader behavior or GUI history policy changed in this step.

Next quality work requires a converged Native reference at the exact captured
camera, an explicit radiance/ghosting criterion for revealed versus retained
regions, and reversal/settling checkpoints. Finite RGB output or a difference from
the reset control alone must not satisfy that quality gate.

### Step 9B: Independent High-Sample Reference Gate

validate_nrd_disocclusion_quality.py consumes a complete successful Step 9A
sixteen-capture report. It checks the expected case/resource inventory, rejects
duplicates/missing cases, verifies capture/log and executable/fixture/helper/
shader hashes, and repeats the visibility, matrix/MV and history-state checks.
Historical moving evidence is marked sourceEvidenceReexecuted=false; it is not
represented as a new GPU run. The current executable and shader artifacts must
match that evidence.

Four new Native references use the recorded positive/negative camera position
and target, seeds 11 and 29, and 1,024 accumulated samples per seed. The positive
references also serve the matching reset control. No orbit is applied to the
references. Runtime transport settings, sample count/seed, render dimensions and
camera projection are checked. Besides the existing 2e-6 matrix allowance,
independent projected rays must agree within 0.01 pixel.

The existing -PathTracingSamples CLI enforces one sample/frame, then pauses at
the requested accumulated count. Raw metadata at that paused capture reports
the next sample index, 1,024. The first pilot correctly produced 1,024 samples,
but the new validator initially expected a sixteen-sample batch and the previous
batch's index. That pilot is retained as failed in
bin/PathTracingValidation/nrd-disocclusion-quality-20261017. The validator now
checks the actual established CLI contract against both raw metadata and runtime
logs. No quality threshold was changed in response to this pilot.

Before reference acquisition, the numeric fixture criteria were fixed separately
for revealed and retained regions:

- Independent reference-pair NRMSE <= 0.03, normalized by RMS of their mean RGB.
- NRD RMSE against the reference mean must not exceed noisy Native RMSE.
- Norm of mean RGB bias <= 0.05 times norm of reference mean RGB.
- Mean positive red excess <= 0.02 times reference mean green.
- 95th-percentile positive red excess <= 0.05 times reference mean green.

Reference disagreement produces inconclusive-reference, not an NRD failure.
Otherwise all three cases (positive, negative and reset control) and both regions
must pass to produce passed-fixed-fixture. Finite output alone cannot pass.
Tests cover regression, color bias, injected red ghosts, inconsistent references,
malformed/nonfinite/black regions, duplicate/missing source inventory and wrong
reference camera/sample/transport state. Threshold values are regression-tested.

```powershell
python Tests/PathTracing/validate_nrd_disocclusion_quality.py --source <qualified-step9a-directory> --output <new-empty-directory>
```

This uses independent high-sample Monte Carlo consistency, not an analytic
ground-truth proof or a general quality guarantee. One moving checkpoint per
direction does not qualify reversal, settling, animation, moving objects or
subjective appearance. The reset control also restarts SDK sampling/frame state.

Qualified evidence: bin/PathTracingValidation/nrd-disocclusion-quality-r2-20261017
passed-fixed-fixture. All sixteen historical moving captures were hash-verified
and their input/history checks repeated; they were not recaptured. Four fresh
reference captures (two cameras x two independent 1,024-spp seeds) completed with
zero D3D12 errors. Reference-pair NRMSE ranged from 1.380% to 1.473%, below the
fixed 3% consistency limit. For the revealed regions:

| Case | NRD NRMSE | Native 1-spp NRMSE | Mean RGB Bias |
| --- | --- | --- | --- |
| +20 degree continuous | 2.458% | 30.547% | 1.919% |
| -20 degree continuous | 1.388% | 34.148% | 0.718% |
| +20 degree reset control | 2.580% | 30.547% | 1.904% |

Retained-region NRD NRMSE ranged from 1.430% to 1.708%, versus 31.188% to 31.231%
for noisy Native. Maximum mean red excess was 0.104% of reference mean green;
maximum 95th-percentile excess was 0.292%, under the fixed 2%/5% limits. These
errors are measured against the mean of finite high-sample references, not an
exact analytic solution. Both regions of all three cases passed every fixed
criterion. All eighty-eight NRD Python tests passed, including six new quality
validator tests. No application/shader change or rebuild was needed; the source
evidence requires the same already Debug-built executable and shader artifacts.

Next is continuous reversal/settling coverage and subjective review. The numeric
result does not change default-off/experimental NRD support policy or complete
the outstanding general visual acceptance gate.

### Step 9C: Continuous Reversal/Settling Capture Contract

The new diagnostic -PathTracingCapturePlan reuses the existing camera-keyframe
and scheduled-screenshot machinery. It implies PT, requires SceneFile, LogToFile
and a PathTracing DebugPreviewResource, and rejects combinations with the other
capture/camera automation modes. Its outputs are raw .ptbuf files; Reflection
plans remain PNG-only. Relative paths retain the single {variant} token (fixed
to pt for PT plans), traversal rejection and ordered frame/case constraints.
PT paths additionally reject normalized/case-insensitive output collisions on
Windows so separate checkpoints cannot overwrite one another.

The App uses the same frame counter to update the Arcball pose and queue the
capture. Each raw request carries the requested debug-resource name. Scheduled
case/frame/path records are logged as PathTracingTimeline, while the existing
capture completion records confirm output drain. No pause is introduced between
checkpoints, and no NRD renderer/reset policy changes are made. GUI camera edits
still use the ordinary reset policy; the runner explicitly enables the existing
NrdContinuousCameraHistory diagnostic mode.

validate_nrd_timeline.py performs one continuous run per resource (Albedo,
MotionVectors, NrdSceneColor, SceneColor), each with eight checkpoints:

| Case | Frame | Yaw | Native Sample Index |
| --- | --- | --- | --- |
| warm | 20 | 0 | 20 |
| forward | 36 | +16 | 0 |
| reverse | 44 | +16 | 0 |
| middle | 60 | 0 | 0 |
| stop | 80 | -20 | 0 |
| settle-4 | 84 | -20 | 4 |
| settle-16 | 96 | -20 | 16 |
| settle-32 | 112 | -20 | 32 |

Yaw stays zero through frame 20, reaches +20 at 40, reverses to -20 at 80, then
holds. Forward/reverse must revisit one pose with opposite measured UV motion.
NRD frame index must equal the absolute timeline frame with reset=false, while
Native accumulation resets during motion and resumes after stop. Per-case
resource metadata must match across the four runs. The previous independent
plane visibility, half-format MV, SDK matrix and finite-composition checks apply.
Scheduled and completed capture inventories must contain all eight ordered
cases. The stop frame's revealed/retained regions are reused at settling frames;
changed camera poses or a frozen NRD signal are rejected.

This is input/history and measurement qualification, not reference-based quality
acceptance. qualityVerdict remains not-evaluated. Earlier Step 9B single-orbit
results must not be relabeled as reversal/settling results. A later reference
assessment must use these newly acquired continuous-timeline captures and their
executed binary/shader hashes.

```powershell
python Tests/PathTracing/validate_nrd_timeline.py --output <new-empty-directory>
```

Evidence: bin/PathTracingValidation/nrd-timeline-20261017 passed all thirty-two
raw captures (four continuous resource runs x eight checkpoints), with zero
D3D12 errors. Every SDK frame index matched the scheduled frame, reset stayed
false, and Native sample indices matched the table. Forward and reverse mean UV
X motion were +0.00177442 and -0.00176195 at the same camera pose. Maximum MV
bound-normalized error across all checkpoints was 0.43016, under the unchanged
2 ULP(expected half) + 3e-6 contract. Independent revealed-surface samples were
126 forward, 116 reverse, 122 middle and 252 at stop; stationary checkpoints
correctly revealed no new surfaces.

The stop frame's 252 revealed and 47,610 retained sampled points remained fixed
through settling. NRD output changed while the SDK index advanced through
84/96/112; these changes are measurements, not claims of convergence or reduced
ghosting. qualityVerdict remains not-evaluated. The report records the executed
Debug binary, SDK/PT/conversion/composition shaders, fixture, helper and runner
hashes, each plan/log hash and all raw capture hashes. Debug app and CaptureSession
test builds passed with zero warnings/errors. CaptureSession tests cover PT CLI
parsing, missing values, unsafe/duplicate paths including Windows aliases,
ordered frames/unique cases, and unchanged Reflection PNG loading. All ninety-one
NRD Python tests passed, including three timeline tests for reversal/settling
schedule, capture inventory and the separate NRD/Native clocks.

The PT camera automation now releases with the existing automation gate after
output completes or fails, rather than continuing to force a pose if the process
is kept open. The final Debug executable including this guard independently
passed all thirty-two captures again in
bin/PathTracingValidation/nrd-timeline-final-20261017, with zero D3D12 errors.
That final run, rather than the earlier executable, is the current checkpoint
evidence. Its captures were also compared with the earlier run to check that the
completion guard did not alter active timeline rendering: all thirty-two raw
files were hash-identical. Mouse interaction
after completion was not exercised by the exit-after-capture runner.

Next: evaluate these continuous captures against fresh same-camera independent
high-sample Native references, including the original stop region at each
settling checkpoint. Do not reuse the earlier single-orbit quality verdict as
that result.
