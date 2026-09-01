# DLSS SR / RR Investigation

Working checkout: `C:\work\RtPbrSurvey-work-3`

Branch: `codex/dlss-sr-integration`

Base observed at creation: cloned from the RtPbrSurvey worktree used for the DLSS investigation thread.

## Goal

Evaluate how to introduce NVIDIA DLSS Super Resolution (SR) and DLSS Ray Reconstruction (RR) into RtPbrSurvey without disrupting the current DX12 renderer and render graph work.

Also keep the temporal-upscaler design open enough to switch between other SR technologies over time. DLSS should be the first investigated backend, not the public shape of the renderer-facing abstraction. NVIDIA headers, SDK types, and binary loading details should stay localized to a narrow integration layer, with a future path toward moving external upscaler backends behind plugin DLL boundaries.

## Worktree Split

Current split between the active renderer/resource work and the DLSS investigation work:

- Work-2 (`C:\work\RtPbrSurvey-work-2`, `codex/renderer-resource-plumbing`) owns renderer core plumbing:
  - render-size/output-size separation
  - render-sized resource specs and lifetime plumbing
  - RenderGraph pass insertion boundary between `LightPass.RenderTarget` and `ToneMapPass`
  - descriptor binding cleanup needed for render graph owned resources
- Work-3 (`C:\work\RtPbrSurvey-work-3`, `codex/dlss-sr-integration`) owns backend/integration-edge work:
  - temporal upscaler support/status shell
  - future `StreamlineAdapter` or equivalent narrow integration layer
  - SDK include/library/binary loading policy
  - DLSS/Streamline-specific docs and support checks

The Work-3 support shell was cherry-picked into Work-2 so renderer plumbing can depend on the neutral `TemporalUpscalerSupport` surface without taking Streamline headers.

## Current Renderer Fit

The current deferred path already has several DLSS SR prerequisites:

- `GBuffer.MotionVector` exists as `DXGI_FORMAT_R16G16_FLOAT`.
- `shaders_GBuffer.hlsl` writes motion vectors from current and previous clip positions.
- `ConstantBuffer` contains `viewProjection`, `prevViewProjection`, and `invViewProjection`.
- `LightPass.RenderTarget` is HDR scene color and is consumed by `ToneMapPass`.
- `ToneMapPass` writes the swap-chain back buffer, so an SR pass can naturally sit between `LightPass` and `ToneMapPass`.

The current RR fit is less direct:

- `HybridReflectionPass` currently writes `ReflectionRayHit` and `ReflectionRayColor`.
- `LightingPass` can consume reflection hit information for overlays and reflection contribution.
- There is no explicit raw radiance/noisy ray output contract yet.
- There is no DLSS-facing material/normal/roughness packaging layer yet beyond GBuffer SRVs.

## External SDK Direction

NVIDIA's current developer page recommends Streamline for DLSS integration. Streamline provides plugins for DLSS Super Resolution and DLSS Ray Reconstruction, and NVIDIA describes DLSS SR as reconstructing higher-resolution output from lower-resolution input using motion data and prior-frame feedback. NVIDIA describes RR as replacing hand-tuned denoisers for ray-traced scenes.

The public Streamline DLSS programming guide says DLSS SR integration requires:

- Initialize Streamline early, before DXGI/D3D APIs are used.
- Set the D3D device after creation.
- Query DLSS feature support per adapter.
- Use `slDLSSGetOptimalSettings()` to choose render resolution from output resolution and quality mode.
- Tag required resources: depth, motion vectors, render-resolution input color, and final-resolution output color.
- Provide per-frame common constants, including motion-vector scale and camera/jitter data.
- Call `slEvaluateFeature(sl::kFeatureDLSS, ...)` at the upscaling point.
- Restore command-list state after evaluation.

Sources checked:

- NVIDIA DLSS developer page: https://developer.nvidia.com/rtx/dlss
- NVIDIA Streamline repository: https://github.com/NVIDIA-RTX/Streamline
- Streamline DLSS programming guide: https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md

## Proposed SR Shape

Add SR in small, reversible steps:

1. Keep the renderer-facing support layer neutral (`TemporalUpscalerSupport`) and compile-safe without SDK headers.
2. Add a backend-specific adapter later, for example `StreamlineAdapter`, as the only place that includes Streamline headers.
3. Add temporal upscaler settings under App/Engine UI ownership: enabled flag, backend/mode, render scale, sharpness/preset, auto exposure toggle, debug state.
4. Add an intermediate output-resolution upscaler output resource, for example `TemporalUpscaler.SceneColor`.
5. Change the render graph path from:

   `LightPass.RenderTarget -> ToneMapPass -> BackBuffer`

   to:

   `LightPass.RenderTarget -> TemporalUpscalerPass -> TemporalUpscaler.SceneColor -> ToneMapPass -> BackBuffer`

6. Keep the native path as the default fallback:

   `LightPass.RenderTarget -> ToneMapPass -> BackBuffer`

Important implementation detail: DLSS SR generally expects render-resolution inputs and final-resolution output. RtPbrSurvey currently builds most resources at `m_width` / `m_height`, which are also the presentation size. A real SR implementation needs separate render size and output size plumbing before quality modes can be meaningful.

Current Work-2 status:

- `TemporalUpscalerSettings` exists with an enabled flag and render scale.
- `m_renderWidth` / `m_renderHeight` are split from output `m_width` / `m_height`.
- GBuffer, depth, LightPass, ShadowMask, reflection resources, compute dispatch sizes, and pixel pick use render size.
- Swap chain, back buffer, ImGui, and ToneMap destination stay output sized.
- `ToneMapPass` now asks the engine for its scene-color resource and descriptor, so the future upscaler output can be inserted without changing ToneMap authoring again.
- `TemporalUpscaler.SceneColor` exists as an output-size transient render texture with RTV/SRV plumbing.
- `TemporalUpscalerPass` exists as a disabled identity-copy stub for the scale 1.0 case.
- When Streamline DLSS SR is enabled and supported, `slDLSSGetOptimalSettings()` now selects `m_renderWidth` / `m_renderHeight` from the output size and quality mode.
- Temporal-upscaler mode or scale changes are deferred through the existing pending-resize path so render-size resources are rebuilt before the new dimensions are used.
- The upscaler pass is not active yet; `HasTemporalUpscalerPassOutput()` remains false until the backend/support path is ready.
- Color render texture binding for `LightPass.RenderTarget`, `ReflectionEvaluatedRadiance`, and `TemporalUpscaler.SceneColor` is table-driven, reducing one-off descriptor setup in `RtPbrSurveyEngine`.
- `RenderTextureSpec` carries basic RTV/SRV creation metadata, so view format ownership is no longer duplicated in the engine-side binding table.
- Shared HDR color render texture specs are helper-built, keeping `LightPass.RenderTarget`, `ReflectionEvaluatedRadiance`, and `TemporalUpscaler.SceneColor` aligned.

## Work-2 Renderer Plumbing Handoff

Work-2's branch goal is to stop before backend SDK integration and leave a stable renderer-facing insertion point:

- Render-resolution resources are separated from output-resolution presentation resources.
- RenderGraph can express the native path and the future temporal-upscaler path.
- `TemporalUpscaler.SceneColor` is the output-resolution handoff resource consumed by `ToneMapPass`.
- `TemporalUpscalerPass` is present but disabled until Work-3 or a later branch provides a real backend/support condition.
- SDK-specific concepts remain outside the broad engine, app, scene, and render graph headers.

Work-3 should build on this by providing backend-side support and evaluation:

- Add or extend a narrow `StreamlineAdapter` / backend adapter layer.
- Keep NVIDIA and Streamline headers in that adapter layer only.
- Translate renderer-owned inputs (`LightPass.RenderTarget`, depth, motion vectors, exposure/jitter constants, `TemporalUpscaler.SceneColor`) into backend calls.
- Decide when `HasTemporalUpscalerPassOutput()` can become true, or provide a backend status that Work-2 can consume in a later handoff.

Remaining renderer-side follow-ups after this branch:

- Camera jitter and non-jittered matrix plumbing.
- Motion-vector convention verification for the chosen backend.
- Exposure resource or auto-exposure policy.
- Optional runtime validation for identity `TemporalUpscalerPass` at render scale 1.0.
- Broader descriptor/resource lifetime ownership cleanup beyond the current color render texture trial.

## Work-2 Final Verification

Final Work-2 checks before PR:

- Debug x64 MSBuild succeeds.
- The app was launched and basic runtime behavior was checked manually on 2026-07-16.
- The branch still leaves `TemporalUpscalerPass` disabled by default, so the native rendering path remains the active path.
- No NVIDIA, DLSS, or Streamline SDK headers are exposed through broad renderer/app/scene headers.

## Future Temporal Upscaler Direction

Keep the first SR work DLSS-focused, but avoid baking DLSS-specific concepts into the renderer-facing architecture where a neutral temporal-upscaler surface would be enough. Future alternatives under consideration include FSR4 SR, Intel SR, and a simple in-repo TAAU path. The UI and settings model should expose a backend or mode choice once more than one implementation exists, while preserving a native/no-upscaler fallback.

NVIDIA Streamline headers and SDK-facing structs should not leak into broad renderer, app, or scene headers. Prefer a small adapter layer that translates from RtPbrSurvey-owned temporal-upscaler inputs into the backend-specific calls. This keeps compile-time impact local, makes it easier to build without SDK artifacts, and leaves room for a later Plugin DLL boundary where each external upscaler owns its SDK includes, binary loading, support queries, and evaluation calls.

## Streamline Adapter Boundary

`Renderer/StreamlineAdapter.h/.cpp` is the intended narrow boundary for NVIDIA-specific integration. The current adapter is a compile-safe stub with no SDK dependency:

- `QueryStreamlineSupport()` returns `TemporalUpscalerBackend::Streamline`.
- `available` remains `false` until SDK policy, artifact location, and support-query implementation are decided.
- `status` is `NotIntegrated`, which the Debug UI reports as `SDK not integrated`.
- `EvaluateStreamline()` accepts SDK-neutral frame input/output resource pointers and dimensions but returns `NotIntegrated` without touching the command list.
- No NVIDIA or Streamline headers are included.
- No SDK types appear in `Engine`, `App`, `Scene`, RenderGraph public interfaces, or broad renderer headers.

Future SDK-backed code should keep these rules:

- `TemporalUpscalerSupport.h` stays renderer-owned and SDK-neutral.
- `StreamlineAdapter.cpp` is the only translation unit that includes Streamline headers.
- If a Streamline declaration must appear outside the implementation file, hide it behind an opaque private type or move the boundary to a plugin DLL.
- SDK binary discovery, `slInit`, plugin loading, feature support queries, resource tagging, and feature evaluation all belong behind the adapter.
- The adapter should translate from RtPbrSurvey-owned resource handles and frame constants into backend calls; it should not make the renderer graph speak Streamline.

## DLSS SR Input Contract

The renderer-facing SR contract should stay backend-neutral, but it needs enough data for DLSS SR:

- Input scene color: `LightPass.RenderTarget`, render resolution, HDR linear color before tone mapping.
- Output scene color: `TemporalUpscaler.SceneColor`, output resolution, HDR linear color before tone mapping.
- Depth: render-resolution scene depth matching the GBuffer and input scene color.
- Motion vectors: `GBuffer.MotionVector`, render resolution. The current convention is NDC delta and must be verified against Streamline's expected scale/sign before enabling DLSS.
- Camera constants: current view/projection, previous view/projection, inverse view/projection, camera near/far if required by the backend.
- Jitter constants: current jitter offset, previous jitter offset, reset/history-invalid flag. This is not yet fully plumbed.
- Exposure: either Streamline auto exposure initially, or a renderer-owned exposure resource once the tone/exposure path has one.
- Render/output dimensions: render width/height and output width/height, derived from `TemporalUpscalerSettings`.
- Quality settings: backend, enabled flag, render scale or quality mode, sharpness/preset, auto exposure policy.

The adapter should not activate the upscaler until the contract can provide all required SR inputs or can explicitly report which requirement is missing.

Current Work-3 status:

- `TemporalUpscalerFrameConstants` carries SDK-neutral row-major camera matrices, camera basis and projection data from the renderer to the adapter.
- `EvaluateStreamline()` obtains a frame token, submits per-frame constants, and tags scene color, depth, motion vectors, and output color with frame-based resource tagging.
- The current motion-vector shader writes `curNdc - prevNdc`, so the adapter submits a motion-vector scale of `(1, 1)` and reports that camera motion is included.
- Camera jitter remains `(0, 0)` until a stable renderer-owned jitter sequence is added.
- `EvaluateStreamline()` sets DLSS options and calls `slEvaluateFeature()` with the same frame token and viewport used for constants and resource tags.
- The adapter reports `outputAvailable=true` only after evaluation succeeds; the renderer then selects `TemporalUpscaler.SceneColor` for tone mapping.

## DLSS RR Input Contract

RR should remain second-phase work. Before SDK wiring, the renderer needs a stable reflection/ray signal contract:

- Raw ray hit signal: hit distance, hit/miss flag, hit normal or encoded normal, and any mask needed for validity.
- Current-frame evaluated reflection radiance: `ReflectionEvaluatedRadiance` is the unweighted, pre-temporal buffer to follow.
- Visible-surface data: depth, normals, roughness/metallic/material information, and motion vectors at render resolution.
- Scene color context: the pre-tonemap lighting result or reflection contribution boundary chosen for RR.
- History/reset state: camera cuts, scene changes, and render-size changes should invalidate RR history.
- Debug views: existing hit/distance/normal overlays should remain available while RR inputs are validated.

The current safer placement remains reconstructing `ReflectionEvaluatedRadiance` before `LightPass`, because final scene color composition stays owned by `LightPass`.

## Future Plugin DLL Boundary

If external upscalers move behind plugin DLLs, keep the host/plugin contract RtPbrSurvey-owned:

- Host-owned inputs: D3D12 device, command queue or command list access policy, source/destination resources, descriptor handles or descriptor allocation callbacks, frame constants, dimensions, and settings.
- Plugin-owned details: vendor SDK headers, SDK DLL loading, plugin DLL loading, support query implementation, backend-specific constants, resource tagging, and evaluation calls.
- Host-visible results: support status, fallback reason, recommended render scale, whether history must reset, and whether the backend produced a valid output resource for `TemporalUpscaler.SceneColor`.
- Versioning: use an explicit ABI/version field if the boundary becomes binary. Do not expose STL types, COM smart pointers, or vendor SDK structs across the DLL boundary.

This keeps the renderer core compatible with multiple backends while allowing each external integration to carry its own licensing and redistribution policy.

## Proposed RR Shape

Hybrid Reflection coordination result:

- raw/debug/resolved separation is already partly underway.
- formal DLSS RR contracts are still later work.
- the next natural Hybrid Reflection step is resource naming and semantic documentation around raw hit payload versus evaluated radiance.

Current resources to track:

- `ReflectionRayHit`: raw ray signal. Hit distance, hit flag, encoded hit normal.
- `ReflectionRayColor`: debug/payload. Hit albedo.
- `ReflectionRayMaterial`: debug/payload. Hit metallic, roughness, unlit flag.
- `ReflectionRayEmission`: debug/payload. Hit emissive.
- `ReflectionEvaluatedRadiance`: unweighted current-frame radiance before temporal processing and `LightPass` contribution weighting.
- `LightPass.RenderTarget`: final scene color after reflected contribution is composited.

For DLSS RR investigation, treat `ReflectionEvaluatedRadiance` as the evaluated input boundary, not as an already resolved signal. A future `ReflectionResolvedRadiance` would preserve the same unweighted semantics after temporal or reconstruction processing.

Do not start RR by wiring Streamline first. Start by clarifying renderer contracts:

1. Split `HybridReflectionPass` outputs into explicit semantic resources:
   - hit/debug output
   - raw/noisy reflection radiance
   - any resolved reflection contribution used by the lighting pass
2. Decide whether RR replaces only reflection denoising or becomes a broader ray-traced lighting reconstruction step.
3. Preserve the current debug overlays, because they are useful for validating RR input correctness.
4. Only after the raw ray signal contract is stable, add a `DlssRayReconstructionPass` wrapper around Streamline.

RR should probably remain behind a runtime support check and a separate UI toggle from SR, even if both use Streamline.

The core RR placement question is still open:

- Replace or reconstruct `ReflectionEvaluatedRadiance` before `LightPass`.
- Or reconstruct a post-lighting reflection contribution after `LightPass`.

The first option is currently safer because it follows the Hybrid Reflection plan and keeps final scene color composition owned by `LightPass`.

## Gaps To Resolve Before Code Integration

- SDK acquisition policy: Streamline binary artifacts and DLSS plugin DLLs are not currently in this repo.
- Licensing and redistribution policy for DLLs must be decided before committing SDK artifacts.
- DLSS quality modes can now select render size, but no user-facing enable path or real Streamline evaluation is active yet.
- Camera jitter is not yet visible in the current GBuffer path. DLSS-quality SR needs a stable jitter sequence and matching non-jittered matrices in Streamline constants.
- Motion vector convention is now represented as NDC delta (`curNdc - prevNdc`) in `R16G16_FLOAT` with Streamline motion-vector scale `(1, 1)`; runtime image validation is still required once evaluation is enabled.
- Exposure path needs a decision: use Streamline auto exposure initially, or add a 1x1 exposure texture from the tone-mapping/exposure settings.
- Resource state ownership must account for Streamline managing tagged resources and command-list state changes.
- UI should expose support state and fallback reason, not just an enable checkbox.
- RR input contract should be written down before SDK work: raw ray hit distance/mask/normal, `ReflectionEvaluatedRadiance`, visible depth/normal/roughness/motion vector, and resolved scene color before tone mapping.

## First Implementation Status

The safest first code step was not full DLSS. It was a compile-safe feature shell:

- `Renderer/TemporalUpscalerSupport.h/.cpp`
- runtime support enum and status string
- no SDK dependency by default
- App/Debug UI section showing "Temporal Upscaler: Unavailable (None, SDK not integrated)"
- render graph remains native by default

Then add SDK-backed code in a separate commit once the external dependency location is decided.

## Recommendation

Proceed with SR first. The renderer already has color, depth, and motion-vector inputs, and the insertion point before tone mapping is clear.

Treat RR as a second phase after the hybrid reflection outputs are renamed or split into raw/debug/resolved semantics. RR will be easier to validate once reflection signal ownership is explicit.

## Work-2 DLSS RR Phase 1 Snapshot

Base and branch:

- Branch: `codex/dlss-ray-reconstruction`
- Base: `origin/main` at `5be2290d79b152a7966f99ad7e02d1f863435d22`
- Scope: SDK-neutral support/query surface, read-only diagnostics, and renderer contract documentation. No RR evaluation is enabled in Phase 1.

Current reflection/resource mapping:

| Role | Current resource | Format | Resolution | Color/space | Notes |
|---|---|---:|---:|---|---|
| Raw ray hit signal | `ReflectionRayHit` | shader-owned UAV payload | render | world/linear payload | Carries hit flag, distance, and encoded hit normal for debug/evaluation. Exact packing remains `HybridReflectionPass` owned. |
| Hit albedo payload | `ReflectionRayColor` | shader-owned UAV payload | render | linear albedo | Debug/payload input to `ReflectionEvaluatePass`; not reflected radiance. |
| Hit material payload | `ReflectionRayMaterial` | shader-owned UAV payload | render | scalar material params | Carries hit metallic, roughness, and unlit/debug flag. |
| Hit emissive payload | `ReflectionRayEmission` | shader-owned UAV payload | render | linear HDR | Emissive input to reflected hit shading. |
| Evaluated reflection input | `ReflectionEvaluatedRadiance` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear HDR | Unweighted one-bounce radiance before temporal processing and `LightPass` contribution weights. Best current RR input candidate. |
| Current-frame specular estimate | `ReflectionSpecularEstimate` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear HDR diagnostic | Weighted Cook-Torrance estimate used for temporal variance/confidence diagnostics. |
| RR roughness input | `ReflectionRoughness` | `DXGI_FORMAT_R8_UNORM` | render | linear scalar [0,1] | RR-only prepare target. Extracted from `GBuffer.PBRParams.g` so Streamline receives roughness in the standalone texture R channel. |
| RR specular albedo input | `ReflectionSpecularAlbedo` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear RGB reflectance | RR-only prepare target. Derived from visible-surface base color and metallic with the shared PBR F0 equation. |
| RR specular hit distance input | `ReflectionSpecularHitDistance` | `DXGI_FORMAT_R16_FLOAT` | render | world-space scalar distance | RR-only prepare target. Extracted from `ReflectionRayHit.x` when `ReflectionRayHit.y > 0`; miss/gated pixels are encoded as `0.0`. |
| Resolved reflection output | `ReflectionResolvedRadiance.0/1` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear HDR | Ping-pong output currently owned by `TemporalReflectionPass`. Future RR output should preserve this unweighted radiance contract. |
| Optional spatially filtered output | `ReflectionDenoisedRadiance` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear HDR | Edge-aware post-temporal variant consumed by `LightPass` only when surface variance filtering is enabled. |
| Final scene color before SR | `LightPass.RenderTarget` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | linear HDR | Includes lighting and enabled reflection contribution, before tone mapping and DLSS SR. |
| DLSS SR output | `TemporalUpscaler.SceneColor` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | output | linear HDR | Output-size pre-tonemap scene color. Not an RR resource. |

Current visible-surface inputs relevant to RR:

| Input | Current resource | Format | Resolution | Representation |
|---|---|---:|---:|---|
| Albedo | `GBuffer.Albedo` | `DXGI_FORMAT_R8G8B8A8_UNORM` | render | Linearized material base color. |
| Normal | `GBuffer.Normal` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | World-space normal, stored as xyz. |
| Material id | `GBuffer.Material` | `DXGI_FORMAT_R32_UINT` | render | Material index. |
| Motion vector | `GBuffer.MotionVector` | `DXGI_FORMAT_R16G16_FLOAT` | render | `prevNdc - curNdc + jitterCancellation + valueOffset`; temporal reflection removes the configured offsets before reprojection. |
| PBR params | `GBuffer.PBRParams` | `DXGI_FORMAT_R8G8B8A8_UNORM` | render | R=metallic, G=roughness, B=ambient occlusion. |
| Emissive | `GBuffer.Emissive` | `DXGI_FORMAT_R16G16B16A16_FLOAT` | render | Linear HDR emissive. |
| Depth | `DepthStencil` | `DXGI_FORMAT_R32_TYPELESS`, SRV `DXGI_FORMAT_R32_FLOAT`, DSV `DXGI_FORMAT_D32_FLOAT` | render | Hardware depth, cleared to 1.0. Current projection is not inverted. |

History, reset, and ownership:

- `TemporalReflectionPass` owns `ReflectionResolvedRadiance.*`, history depth, history normal, specular estimate history, moments, and confidence today.
- `InvalidateReflectionHistory()` resets the reflection history for lighting/material/reflection setting changes, scene changes, resize, and diagnostic reset paths.
- DLSS RR and the existing temporal reflection pass must be mutually exclusive once RR evaluation is implemented. Phase 1 records the policy but leaves the existing temporal reflection path active.
- The future boundary is `ReflectionEvaluatedRadiance -> ReflectionResolvedRadiance -> LightPass composition`. `LightPass` should continue applying visible-surface Fresnel, roughness/contribution weighting, distance fade, and user intensity.

## Work-2 DLSS RR Phase 2 Boundary Shell

The first Phase 2 code step makes the future producer boundary explicit without changing the default renderer path:

- Default: `ReflectionEvaluatePass -> TemporalReflectionPass -> ReflectionResolvedRadiance -> LightPass`
- RR path when explicitly enabled and supported: `ReflectionEvaluatePass -> DlssRayReconstructionPass -> ReflectionResolvedRadiance -> LightPass`

The two resolved-radiance producers are mutually exclusive in `AddSceneRenderPasses()`. `DlssRayReconstructionPass` currently records the intended resource boundary and uses a copy fallback from `ReflectionEvaluatedRadiance` to the current `ReflectionResolvedRadiance` ping-pong target. It does not call Streamline RR yet.

Current shell inputs:

- `ReflectionEvaluatedRadiance` as `COPY_SOURCE`
- `ReflectionEvaluatedRadiance` as `ScalingInputColor` candidate
- `DepthStencil` as shader-readable depth
- `GBuffer.Albedo`
- `ReflectionSpecularAlbedo`, generated from `GBuffer.Albedo` and `GBuffer.PBRParams.r`
- `GBuffer.Normal`
- `GBuffer.MotionVector`
- `ReflectionRoughness`, generated from `GBuffer.PBRParams.g`
- `ReflectionSpecularHitDistance`, generated from `ReflectionRayHit.x/y`

Current shell output:

- `ReflectionResolvedRadiance.{writeIndex}` as `COPY_DEST`

This keeps the LightPass contract stable while making the RR insertion point visible in RenderGraph captures. Phase 2 implementation should replace the copy fallback with SDK evaluation and should decide whether auxiliary reflection/specular histories remain temporal-only diagnostics or receive RR-owned equivalents.

## Work-2 Guarded Native RR Evaluate

The guarded native path is now present but remains opt-in:

1. Enable Hybrid Reflection and use the deferred path.
2. Confirm `DLSS Ray Reconstruction` reports `Available`.
3. Enable `RR Enabled`.
4. Enable `Experimental Native Evaluate`.

Automation flags:

- `-EnableDlssRayReconstruction` enables the RR render-graph path and keeps native evaluate disabled.
- `-EnableExperimentalNativeRayReconstruction` enables the guarded native evaluate path and implies RR enabled. Validation commands should pass both flags so the requested state is clear in command history.
- CLI RR flags are runtime-only settings overrides. They do not write scene config.

If `Experimental Native Evaluate` is off, if readiness is not `Ready`, or if any Streamline call fails, `DlssRayReconstructionPass` copies `ReflectionEvaluatedRadiance` into the current `ReflectionResolvedRadiance` target for that frame. The UI reports the last result as either `Native Output` or `Copy Fallback`.

The native branch calls Streamline only after support is available, RR is enabled, native evaluation is explicitly enabled, and the SDK-neutral input readiness check passes. Success from `slEvaluateFeature(sl::kFeatureDLSS_RR, ...)` is the only condition that marks `ReflectionResolvedRadiance` as produced by native RR.

`ReflectionResolvedRadiance.0/1` remains `DXGI_FORMAT_R16G16B16A16_FLOAT` at render resolution. It now allows UAV creation so the resource is compatible with a native RR output path, while the current fallback transition remains `COPY_DEST`.

RTX 2080 remains a supported test target for failure behavior: `slIsFeatureSupported(sl::kFeatureDLSS_RR, ...)` may report unavailable or unsupported depending on driver/runtime support. In that case the RR UI controls stay disabled and the normal temporal reflection path remains the owner of `ReflectionResolvedRadiance`.

Expected debug-layer check:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -LogToFile d3d12_debug.log -LogFPS 120
Select-String -LiteralPath d3d12_debug.log -Pattern "\[ERROR\]|\[WARNING\]|D3D12"
```

Expected CLI validation commands:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_copy_fallback.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_copy_fallback.log
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_native.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_native.log
```

`-LogToFile` writes `[RR]` state-change lines containing support status, raw Streamline support-query result, input readiness, last evaluate status, raw last Streamline/evaluate result, and whether the current result came from native output or copy fallback.

RTX 2080 validation on this branch:

- Base commit before CLI validation: `af646fe Add guarded native ray reconstruction evaluate path`.
- CLI validation commit: `5dc35ef Add ray reconstruction CLI validation flags`.
- `-EnableDlssRayReconstruction` with native evaluate off exited with code 0 and produced a 1920x1080 capture.
- `-EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction` also exited with code 0 and produced a 1920x1080 capture.
- Both runs reported `support=unavailable status=Unsupported adapter`, so `DlssRayReconstructionPass` and `slEvaluateFeature(sl::kFeatureDLSS_RR, ...)` were not reached on this RTX 2080 setup.
- After raw Streamline result logging was added, the support query result on this RTX 2080 setup was `Result::eErrorFeatureNotSupported`.
- Both captures were non-black by sampled pixel check. The two captures matched the same unsupported-adapter fallback behavior.
- D3D12 debug output contained no `ERROR`; it did contain two existing `CreateCommittedResource` warnings about buffer initial state `UNORDERED_ACCESS` being treated as `COMMON`.

The default toggle-off path should not emit D3D12 errors. Native evaluate still needs image validation for motion-vector sign/scale, normal-space interpretation, and whether `ReflectionEvaluatedRadiance` is acceptable as Streamline's noisy specular input.

## Work-2 RR Streamline 2.12.0 API Notes

Header check against `C:\work\third_party\streamline-sdk-2.12.0\include`:

- Feature id: `sl::kFeatureDLSS_RR` in `sl_core_types.h`.
- RR API header: `sl_dlss_d.h`.
- RR option type: `sl::DLSSDOptions`.
- RR option call: `slDLSSDSetOptions(viewport, options)`.
- RR state call: `slDLSSDGetState(viewport, state)`.
- RR evaluate call: `slEvaluateFeature(sl::kFeatureDLSS_RR, frameToken, evaluateInputs, count, commandList)`.
- Common constants still use `sl::Constants`, `slSetConstants(constants, frameToken, viewport)`, and frame-based resource tagging.
- Required matrices must be row-major and non-jittered. `DLSSDOptions` additionally requires `worldToCameraView` and `cameraViewToWorld`.
- Normal/roughness mode can be `DLSSDNormalRoughnessMode::eUnpacked` or `ePacked`. Current scaffold uses `eUnpacked`.

Relevant Streamline buffer tags:

| Streamline tag | Classification | Current candidate | Status |
|---|---|---|---|
| `kBufferTypeScalingInputColor` | Required | `ReflectionEvaluatedRadiance` | Candidate. DLSS-RR guide calls this the noisy ray-traced input color. Current content is unweighted one-bounce reflection radiance and still needs runtime image validation. |
| `kBufferTypeScalingOutputColor` | Required | `ReflectionResolvedRadiance.{writeIndex}` | Candidate output. This is the current native scaffold output tag. |
| `kBufferTypeAlbedo` | Required | `GBuffer.Albedo` | Candidate. Format is `R8G8B8A8_UNORM`, linearized material base color by renderer convention. |
| `kBufferTypeSpecularAlbedo` | Required | `ReflectionSpecularAlbedo` | Available as a standalone `R16G16B16A16_FLOAT` render-size texture. It stores visible-surface `PbrF0(albedo, metallic)` in linear RGB. Alpha is `1.0` and not used by the contract. |
| `kBufferTypeSpecularHitNoisy` | Not part of the DLSS-RR minimum path | none | Present in Streamline core buffer ids but not required by the DLSS-RR 2.12.0 guide or plugin source path checked for this branch. |
| `kBufferTypeSpecularHitDenoised` | Not part of the DLSS-RR minimum path | none | Present in Streamline core buffer ids but not used as the current native scaffold output tag. `ScalingOutputColor` carries the RR output. |
| `kBufferTypeDepth` or `kBufferTypeLinearDepth` | Required | `DepthStencil` SRV | Available as hardware depth. Current scaffold uses hardware depth with camera constants. |
| `kBufferTypeMotionVectors` | Required | `GBuffer.MotionVector` | Available. Current convention is `prevNdc - curNdc + jitterCancellation + valueOffset`; adapter exposes scale/offset, but native RR sign/scale still needs image validation. |
| `kBufferTypeNormals` or `kBufferTypeNormalRoughness` | Required by selected normal/roughness mode | `GBuffer.Normal` | Available. Current scaffold selects unpacked mode. Current normal is world space xyz in `R16G16B16A16_FLOAT`; `DLSSDOptions` receives world/view matrices so the space is explicit. |
| `kBufferTypeRoughness` | Required by selected normal/roughness mode | `ReflectionRoughness` | Available as a standalone `R8_UNORM` render-size texture. Values are copied from `GBuffer.PBRParams.g` into the texture R channel. |
| `kBufferTypeSpecularHitDistance` | Recommended when specular motion vectors are absent | `ReflectionSpecularHitDistance` | Available as a standalone `R16_FLOAT` render-size texture. Values are world-space hit distance from the primary reflection ray origin to hit point; miss/gated pixels are `0.0`. |
| `kBufferTypeSpecularMotionVectors` | Recommended alternative | none | Optional in plugin source. DLSS-RR guide says the app can provide specular motion vectors directly or provide specular hit distance with matrices. |
| `kBufferTypeSpecularRayDirectionHitDistance` | Optional alternative | none | Optional packed direction+distance input. Not wired. |
| `kBufferTypeReflectionMotionVectors` | Optional | none | Optional. No reflection-specific motion vector resource exists yet. |
| `kBufferTypeReflectedAlbedo` | Optional | `ReflectionRayColor` | Available as hit albedo payload. Not wired into the native evaluate scaffold yet. |
| `kBufferTypeDisocclusionMask` | Optional | none | Optional in plugin source. Existing temporal pass derives rejection from depth/normal history internally. |
| `kBufferTypeExposure` | Optional | none | Optional in plugin source. Current scaffold uses `DLSSDOptions::preExposure=1.0` and `exposureScale=toneMap.exposure`; no renderer-owned exposure texture is required for minimum legality. |

Scaffold order in `StreamlineAdapter.cpp`:

1. Check RR support state from `slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo)`.
2. Validate SDK-neutral `RayReconstructionEvaluateInputs` and store the last readiness reason in `RayReconstructionDiagnostics`.
3. Return unavailable without calling the SDK while inputs are not ready or while `enableNativeEvaluation` is false. This keeps Phase 2 disabled-by-default even when the SDK and adapter support RR.
4. Future native path obtains `slGetNewFrameToken()`.
5. Set `sl::DLSSDOptions` through `slDLSSDSetOptions()`.
6. Set common `sl::Constants` through `slSetConstants()`.
7. Tag candidate resources with `slSetTagForFrame()`.
8. Run `slEvaluateFeature(sl::kFeatureDLSS_RR, ...)`.

Current minimum-input readiness is expected to be true once the RR path runs and all prepare resources have been produced. Native RR evaluate remains disabled because `enableNativeEvaluation` is still false and `ReflectionEvaluatedRadiance` still needs runtime validation against Streamline's noisy input-color expectation.

Exposure is not considered a minimum-readiness blocker for this branch: Streamline 2.12.0 plugin source queries `kBufferTypeExposure` as optional, and DLSS-RR options expose `preExposure` and `exposureScale`.

## Work-2 RR Roughness Prepare Pass

`RayReconstructionRoughnessPass` is emitted only on the RR path, immediately before `DlssRayReconstructionPass`.

- Input: `GBuffer.PBRParams` as pixel-shader SRV.
- Output: `ReflectionRoughness` as render target.
- Format: `DXGI_FORMAT_R8_UNORM`.
- Resolution: render size.
- Value: `saturate(GBuffer.PBRParams.g)`, stored in R channel.
- Purpose: satisfy Streamline's standalone `kBufferTypeRoughness` expectation without changing the existing GBuffer MRT layout.

When RR is disabled or unsupported, this pass is not added to the frame graph and the existing temporal reflection path is unchanged.

## Work-2 RR Specular Albedo Prepare Pass

`RayReconstructionSpecularAlbedoPass` is emitted only on the RR path, immediately before `DlssRayReconstructionPass`.

- Inputs: `GBuffer.Albedo` and `GBuffer.PBRParams` as pixel-shader SRVs.
- Output: `ReflectionSpecularAlbedo` as render target.
- Format: `DXGI_FORMAT_R16G16B16A16_FLOAT`.
- Resolution: render size.
- Value: `PbrF0(saturate(GBuffer.Albedo.rgb), saturate(GBuffer.PBRParams.r))`.
- Formula: `lerp(float3(0.04, 0.04, 0.04), albedo, metallic)`, shared through `PbrLighting.hlsli`.
- Color space: linear RGB. `GBuffer.Albedo` is written by `shaders_GBuffer.hlsl` after `SrgbToLinear()` on sampled base color.
- Alpha: `1.0`; Streamline's specular albedo contract uses RGB.
- Format reason: `R16G16B16A16_FLOAT` keeps the small dielectric F0 range and metallic base-color F0 without relying on normalized 8-bit precision. The input remains linear, not sRGB.

When RR is disabled or unsupported, this pass is not added to the frame graph and the existing temporal reflection path is unchanged.

## Work-2 RR Specular Hit Distance Prepare Pass

`RayReconstructionSpecularHitDistancePass` is emitted only on the RR path, immediately before `DlssRayReconstructionPass`.

- Input: `ReflectionRayHit` as pixel-shader SRV.
- Output: `ReflectionSpecularHitDistance` as render target.
- Format: `DXGI_FORMAT_R16_FLOAT`.
- Resolution: render size.
- Value: `max(ReflectionRayHit.x, 0.0)` when `ReflectionRayHit.y > 0.0`, otherwise `0.0`.
- Distance unit: world-space ray distance from primary-surface reflection ray origin to committed hit point. This matches `RayQuery::CommittedRayT()` because hybrid reflection rays are traced in world space.
- Miss/invalid encoding: `0.0`, matching the current `ReflectionRayHit` miss/gated-pixel convention. Streamline 2.12.0 docs do not define a separate sentinel.
- SDK status: `kBufferTypeSpecularHitDistance` is optional in `sl_core_types.h` and the DLSS-RR plugin queries it as optional. The DLSS-RR guide says it is needed when specular motion vectors are not provided.

When RR is disabled or unsupported, this pass is not added to the frame graph and the existing temporal reflection path is unchanged.

Unmet RR inputs / decisions:

- Decide whether RR consumes only reflection radiance or also needs additional auxiliary raw ray data (`ReflectionRayHit`, hit normal, material payloads, ray directions, or reflection-specific motion vectors).
- Decide whether RR output replaces `TemporalReflectionPass` directly or sits behind a new `ReflectionResolvedRadiance` producer selected by settings.
- Define exposure handling for RR. Current SR path uses Streamline auto exposure; there is no renderer-owned exposure texture yet.
- Validate motion vector sign/scale with the RR SDK. Current renderer convention is explicit, but vendor contract matching still needs runtime validation.
- Add D3D12 Debug Layer runtime validation only when RR evaluation is wired; Phase 1 has no RR resource tagging/evaluate call.
