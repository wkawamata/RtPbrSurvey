# Path Tracer Denoiser Input Contract And Delivery Plan

Date: 2026-10-10. Base: main 0c175e3 (PR 96).
Branch: codex/pt-denoiser-input-contract.
Status: NRD RELAX selected; dependency pin and SDK-free adapter shell added.
No denoiser dispatch or rendering behavior change.

## Current Acceptance State

- [x] Work-3 synchronized to merged main.
- [x] Native PT correctness evidence and qualified limitations preserved.
- [ ] User-assisted integrated GUI review (Step 2): pending, desktop locked.
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
