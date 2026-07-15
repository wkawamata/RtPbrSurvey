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
- The upscaler pass is not active yet; `HasTemporalUpscalerPassOutput()` remains false until the backend/support path is ready.
- Color render texture binding for `LightPass.RenderTarget`, `ReflectionRadiance`, and `TemporalUpscaler.SceneColor` is table-driven, reducing one-off descriptor setup in `RtPbrSurveyEngine`.
- `RenderTextureSpec` carries basic RTV/SRV creation metadata, so view format ownership is no longer duplicated in the engine-side binding table.
- Shared HDR color render texture specs are helper-built, keeping `LightPass.RenderTarget`, `ReflectionRadiance`, and `TemporalUpscaler.SceneColor` aligned.

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
- `ReflectionRadiance`: evaluated reflection radiance before `LightPass`, and before visible-surface Fresnel is applied.
- `LightPass.RenderTarget`: final scene color after reflected contribution is composited.

For DLSS RR investigation, treat `ReflectionRadiance` as the provisional resolved/evaluated reflection buffer to follow. This matches the Hybrid Reflection direction of building a more physical radiance buffer.

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

- Replace or reconstruct `ReflectionRadiance` before `LightPass`.
- Or reconstruct a post-lighting reflection contribution after `LightPass`.

The first option is currently safer because it follows the Hybrid Reflection plan and keeps final scene color composition owned by `LightPass`.

## Gaps To Resolve Before Code Integration

- SDK acquisition policy: Streamline binary artifacts and DLSS plugin DLLs are not currently in this repo.
- Licensing and redistribution policy for DLLs must be decided before committing SDK artifacts.
- Current render size equals output size; SR needs render-size/output-size separation.
- Camera jitter is not yet visible in the current GBuffer path. DLSS-quality SR needs a stable jitter sequence and matching non-jittered matrices in Streamline constants.
- Motion vector convention must be verified. Current shader writes NDC delta (`curNdc - prevNdc`) into `R16G16_FLOAT`; Streamline constants must match this range and sign.
- Exposure path needs a decision: use Streamline auto exposure initially, or add a 1x1 exposure texture from the tone-mapping/exposure settings.
- Resource state ownership must account for Streamline managing tagged resources and command-list state changes.
- UI should expose support state and fallback reason, not just an enable checkbox.
- RR input contract should be written down before SDK work: raw ray hit distance/mask/normal, `ReflectionRadiance`, visible depth/normal/roughness/motion vector, and resolved scene color before tone mapping.

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
