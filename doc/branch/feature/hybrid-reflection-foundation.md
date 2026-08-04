# HybridReflectionPass Design Note

## Overview

Recommendation for the shape of a full-screen HybridReflectionPass using RayQuery inline raytracing to compute specular reflections from the GBuffer.

Current implementation status: HybridReflectionPass is wired into the render graph and writes separate raw hit and material payloads. `ReflectionEvaluatePass` consumes those payloads and produces the pre-composite reflection signal. `LightPass` applies the visible-surface Fresnel term and adds the result to the lit scene.

## Reflection Resource Contract

All resources below are render-resolution `DXGI_FORMAT_R16G16B16A16_FLOAT` textures. The contract describes the current implementation; it does not imply that temporal accumulation, denoising, DLSS Ray Reconstruction, or PathTracing comparison is already integrated.

| Resource | Producer | Meaning and channels | Classification | Current / future consumers |
|----------|----------|----------------------|----------------|----------------------------|
| `ReflectionRayHit` | `HybridReflectionPass` | `.x`: committed ray distance; `.y`: hit flag (`1` hit, `0` miss or gated pixel); `.zw`: oct-encoded world-space hit normal. A zero value is written for background, gated pixels, and misses. | Raw ray signal | `ReflectionEvaluatePass`, reflection debug views, and `LightPass` hit overlays. A future temporal stage may use hit flag, distance, and normal for rejection and confidence. |
| `ReflectionRayColor` | `HybridReflectionPass` | `.rgb`: linear hit albedo; `.a`: `1` for a committed hit and `0` otherwise. Despite the historical name, this is neither emissive nor reflected color. | Material/debug payload | `ReflectionEvaluatePass`, albedo debug view, and the temporary hit-albedo overlay. A future denoiser should not treat it as radiance. |
| `ReflectionRayMaterial` | `HybridReflectionPass` | `.x`: metallic; `.y`: roughness; `.z`: unlit flag; `.w`: reserved. | Material/debug payload | `ReflectionEvaluatePass` and material/component debug views. Roughness is a likely future confidence and filtering input. |
| `ReflectionRayEmission` | `HybridReflectionPass` | `.rgb`: linear hit emissive after material emissive scale; `.a`: `1` for a committed hit and `0` otherwise. | Material/debug payload | `ReflectionEvaluatePass` and emission/component debug views. It remains separate from albedo and evaluated radiance. |
| `ReflectionEvaluatedRadiance` | `ReflectionEvaluatePass` | `.rgb`: linear HDR one-bounce radiance; `.a`: `1`. On a hit it contains evaluated hit-surface direct light, diffuse/specular IBL, emissive, and unlit handling. On a miss or material-gated pixel it contains the roughness-filtered environment fallback. It excludes distance fade, visible-surface roughness weight, user contribution intensity, visible-surface Fresnel, and temporal/spatial filtering. | Evaluated, unweighted radiance | `LightPass` and the evaluated-radiance debug view. This is the current-frame input contract for future temporal accumulation/denoise. |
| LightPass reflection contribution | `LightPass` | The selected unweighted reflection radiance multiplied by distance weight, visible roughness weight, contribution intensity, and visible-surface Fresnel, then additively composed with deferred lighting. The selected source is currently `ReflectionEvaluatedRadiance`; a future enabled temporal stage selects `ReflectionResolvedRadiance`. Miss and material-gated pixels use distance weight `1`. It has no standalone resource. | Final contribution | Final lit scene. It is not a raw denoiser input. |

### Reflection History Contract

This section fixes the resource ownership and reset boundary for a future temporal reflection stage. It does not mean that temporal accumulation, reprojection, rejection, or denoising is implemented.

The temporal stage sits between `ReflectionEvaluatePass` and `LightPass`. It consumes the current-frame `ReflectionEvaluatedRadiance` and, when valid, the previous `ReflectionResolvedRadiance`. It produces the current-frame `ReflectionResolvedRadiance`. When temporal reflection is disabled, `LightPass` consumes `ReflectionEvaluatedRadiance` directly.

`ReflectionResolvedRadiance` has the following semantic contract:

- `.rgb` is linear HDR, unweighted one-bounce radiance after temporal reflection processing;
- `.a` is `1`; history length, confidence, and rejection state are not packed into this channel;
- it preserves the hit and environment-fallback meaning of `ReflectionEvaluatedRadiance`;
- it excludes distance fade, visible-surface roughness weight, user contribution intensity, visible-surface Fresnel, and final LightPass composition;
- it is the only temporally resolved reflection input consumed by `LightPass`.

The engine owns two persistent, render-resolution `DXGI_FORMAT_R16G16B16A16_FLOAT` physical slots for `ReflectionResolvedRadiance`. Their logical roles are `historyRead` and `historyWrite`. A dedicated reflection-history index selects those roles; swap-chain `m_currentFrameIndex` and `m_previousFrameIndex` do not own or select reflection history. `ReflectionEvaluatedRadiance` is current-frame input and is never reused as a history slot.

The roles are exchanged only after the temporal reflection pass successfully produces the current resolved result. Frames that do not run that pass do not advance reflection history. The engine, rather than an individual shader or `LightPass`, owns the history slots, role index, and validity state. Reflection history validity is independent of `m_temporalUpscalerHistoryReset`.

A reset logically invalidates history. It does not require clearing both physical textures. On the first temporal reflection frame after reset, the pass must not sample old history; it produces `ReflectionResolvedRadiance` from the current `ReflectionEvaluatedRadiance`, then marks the result valid and makes it the next `historyRead` slot. A pending reset is cleared only after that output has been produced successfully. If temporal reflection is disabled, history remains invalid.

Hard-reset events are:

- initial history allocation or reallocation;
- output-size or render-size changes;
- scene reload, scene close, or rendering-path changes;
- an explicit camera cut, teleport, or camera-preset change;
- Hybrid Reflection or temporal reflection enable-state changes;
- material, lighting, or environment changes;
- Material Gate or another ray-selection setting change that changes the evaluated signal.

Normal camera movement is not a hard reset; future reprojection handles it. Camera-cut detection is an explicit engine signal in this contract, not a matrix-difference heuristic. Post-resolve controls do not reset history: distance fade, maximum distance, visible-surface roughness weight, contribution intensity, hit overlay, exposure, tone mapping, and debug-view selection.

Future rejection work may consume current and previous depth or reconstructed position, visible-surface normal and roughness, motion vectors, reflection hit flag, hit distance, and hit normal. Their thresholds, packing, auxiliary history resources, and algorithms are deliberately outside this history-ownership phase.

PathTracing comparison should compare signals at a named boundary. `ReflectionEvaluatedRadiance` is the unweighted, pre-temporal and pre-visible-surface-composite boundary; the final LightPass contribution is weighted and embedded in the lit scene. A future reference signal should match one of those boundaries explicitly. DLSS Ray Reconstruction may motivate a different input contract later, but no Streamline or DLSS RR backend is wired here.

## Existing Pass Survey

### RayQueryShadowPass (full-screen, descriptor-table-based)

- **Root signature** (6 params):
  0. UAV descriptor table -- g_shadowMask (u0)
  1. SRV descriptor table -- g_tlas (t0)
  2. SRV descriptor table -- g_depth (t1)
  3. SRV descriptor table -- g_normal (t2)
  4. CBV descriptor table -- CameraCB (b0)
  5. 32-bit constants (11 values, b1) -- lightDirection, normalBias, rayTMin, rayTMax, enabled, softShadowEnabled, sampleCount, lightAngularRadius, jitterStrength
- **PSO**: Compute, thread group 8x8
- **Output**: RWTexture2D<float> (shadow mask via UAV)
- **Render graph**: Reads depth+normal (NON_PIXEL_SHADER_RESOURCE), writes shadow mask (UNORDERED_ACCESS). Placed after GBufferPass, before LightingPass.

### SpecularDebugRayQueryPass (single-pixel, root-descriptor-based)

- **Root signature** (3 params):
  0. UAV root descriptor -- g_result (u0, raw VA)
  1. SRV descriptor table -- g_tlas (t0)
  2. 32-bit constants (8 values, b0) -- rayOrigin (3), rayTMin, rayDirection (3), rayTMax
- **PSO**: Compute, thread group 1x1
- **Output**: RWByteAddressBuffer (32 bytes: hitFlag, hitDistance, hitPosition)
- **Render graph**: No declared Reads/Writes. Manual barriers + CopyResource to readback.
- **Note**: Useful reference for reflection ray setup (origin bias, direction, hit-distance decode).

## Recommended Root Signature Shape

Follow RayQueryShadowPass's descriptor-table approach (identical pattern):

| Param | Type          | Register | Content                          |
|-------|---------------|----------|----------------------------------|
| 0     | UAV table     | u0       | Reflection hit output            |
| 1     | UAV table     | u1       | Reflection color output          |
| 2     | UAV table     | u2       | Reflection material output       |
| 3     | SRV table     | t0       | TLAS                             |
| 4     | SRV table     | t1       | Depth                            |
| 5     | SRV table     | t2       | GBuffer Normal                   |
| 6     | SRV table     | t3       | GBuffer PBR params               |
| 7     | CBV table     | b0       | CameraCB                         |
| 8     | Root SRV      | t4       | Scene vertex buffer bytes        |
| 9     | Root SRV      | t5       | Scene index buffer bytes         |
| 10    | Root SRV      | t6       | Instance buffer bytes            |
| 11    | SRV table     | t7       | Material buffer                  |
| 12    | SRV table     | t0 s8    | Texture table                    |
| 13    | 32-bit consts | b1       | Reflection constants (see below) |

Constants (b1): normalBias, rayTMin, rayTMax, maxRoughness, minMetallic, usesIndexedDraw, vertexCount, indexCount, hitNormalSource.

## PSO Creation

Same pattern as `CreateRayQueryShadowRootSignature` + `D3D12_COMPUTE_PIPELINE_STATE_DESC` with CS bytecode from `shaders_HybridReflection_CSMain.cso`. Store to `m_hybridReflectionRootSignature` / `m_hybridReflectionPipeline`.

## First Shader Output Format

**Current format: raw hit plus separate material payloads**:

- `RWTexture2D<float4>` -- .x = hit distance (0 on miss), .y = hit flag (1.0 hit, 0.0 miss), .z/.w = oct-encoded hit normal.
- `RWTexture2D<float4>` color -- linear hit albedo only. This is not emissive or reflected radiance.
- `RWTexture2D<float4>` material -- hit material payload: metallic, roughness, unlit flag, reserved. This separates material parameters from color so the next reflection-evaluation step can use a BRDF-shaped payload instead of overloading `ReflectionRayColor`.
- `RWTexture2D<float4>` emission -- linear hit emissive after material emissive scale. This remains separate from albedo.
- Rationale: `q.CommittedRayT()` is free to capture, enables temporal denoising (distance-based confidence), and aids debugging.
- Current scaffold uses `ReflectionRayHit` with `DXGI_FORMAT_R16G16B16A16_FLOAT`.
- Hit position can be reconstructed in LightPass as `worldPos + reflectionDir * hitDistance`.
- Hit normal is reconstructed in the ray query shader from `CommittedPrimitiveIndex()`, `CommittedTriangleBarycentrics()`, and the scene vertex/index buffers, then transformed by the committed object-to-world matrix.
- The debug UI can switch hit attribute source between interpolated vertex normal, geometric triangle normal, materialId debug color, material parameter debug color, hit UV debug color, and hit albedo texture debug color for diagnosis.
- Can be packed into R16G16_UNORM later for bandwidth savings if needed, but keep float storage while debugging.

## Material Gating

The HybridReflectionPass can optionally gate traced pixels by GBuffer PBR params:

- `Enabled` controls whether the HybridReflectionPass is added to the render graph.
- Reflection Hit/Distance debug views are disabled when Hybrid Reflection is disabled, avoiding stale hit-buffer inspection.
- `Hit Overlay` is a temporary LightPass composite aid. It tints pixels with `ReflectionRayHit.y > 0` in cyan and does not represent final reflection color.
- `Hit Position Color` changes the overlay tint to `worldPos + reflectionDir * hitDistance` based debug color. This is still a diagnostic color, not reflected surface shading.
- `Environment` overlay mode tints hit pixels with the existing specular prefilter environment sample along the reflection direction. It validates the reflection-direction sampling path, not scene-surface reflection.
- `Hit Normal` overlay mode decodes the hit normal stored in `ReflectionRayHit.zw` and tints hit pixels with normal color over the lit scene.
- `Reflection Material Params` debug view visualizes `ReflectionRayMaterial` as `R = metallic`, `G = roughness`, `B = unlit flag`.
- `Reflection Contribution` lets LightPass consume `ReflectionEvaluatedRadiance`; LightPass applies the visible-surface Fresnel term before adding it.
- `ReflectionEvaluatePass` writes unweighted one-bounce radiance into `ReflectionEvaluatedRadiance`, including direct light, diffuse IBL, specular IBL approximation, emissive, and miss/material-gate environment fallback.
- `LightPass` applies distance fade for hits, visible-surface roughness weight, contribution intensity, and visible-surface Fresnel before additive composition.
- Reflection hit direct lighting now uses the shared PBR direct-light BRDF helper.
- Reflection hit diffuse/specular IBL uses the shared PBR IBL helpers, including BRDF LUT based specular IBL.
- Reflection hit shading is organized as `PbrSurface -> PbrRadianceComponents -> evaluated radiance` in shared HLSL helpers.
- `Evaluated Radiance` debug view visualizes the unweighted `ReflectionEvaluatedRadiance` texture with simple tone mapping.
- Evaluated-radiance component debug views visualize unweighted direct, diffuse IBL, specular IBL, and emissive components recomputed from the hit payload.
- `Reflection Contribution Max Distance` fades the provisional contribution by hit distance, reducing far-hit color bleeding while the reflection color is still approximate.
- Material Gate is disabled by default: `maxRoughness = 1.0`, `minMetallic = 0.0`, preserving the initial "trace all visible pixels" behavior.
- When `Material Gate` is enabled in the Debug UI, the pass uses `HybridReflectionSettings::maxRoughness` and `minMetallic`.
- The first useful debug setting is roughness-focused (`maxRoughness = 0.35`, `minMetallic = 0.0`) so glossy dielectric and metallic surfaces can both be inspected.

## Remaining Reflection Radiance Work

- Decide whether the reflection hit path needs hit ambient occlusion or should keep `ambientOcclusion = 1.0` until a payload/source exists.
- Refine miss fallback if needed; it currently samples the specular prefilter map using visible-surface roughness.
- Keep raw hit payload, evaluated reflection radiance, and debug component views distinct for future DLSS RR and denoising work.
- Consider screen-space resolved color reuse for visible hit points after the one-bounce hit-point shading path is stable.
- Add temporal accumulation / denoise once reflection radiance is physically closer to the intended signal.

## Shared PBR Shader Helpers

- `Shaders/PbrLighting.hlsli` owns shared BRDF math and lightweight shading helpers used by LightPass and reflection evaluation.
- Shared types include `PbrSurface` and `PbrRadianceComponents`.
- Shared helper flow is `EvaluatePbrDirectLighting()`, `EvaluatePbrDiffuseIbl()`, `EvaluatePbrSpecularIbl()`, and `EvaluatePbrSurfaceRadiance()`.
- The current ReflectionEvaluate path keeps texture sampling pass-local, then feeds sampled irradiance/prefilter/BRDF inputs into the shared helpers.
- This is intended as a future PathTracing bridge: ray hit shaders should be able to produce a `PbrSurface` and reuse the same BRDF helpers.

## Descriptors Required

- 4 UAVs: reflection hit, albedo, material, and emission payload output textures
- 1 SRV: TLAS (`m_accelerationStructures.tlasSrv.Gpu()`, register t0)
- 1 SRV: depth buffer
- 1 SRV: GBuffer normal
- 1 SRV: GBuffer PBR params
- 1 CBV: camera constants
- 3 root SRVs: scene vertex/index/instance buffers for committed hit normal and materialId reconstruction
- 1 SRV: material buffer for hit material parameter debug
- 1 SRV table: scene texture table for hit albedo texture debug
- 4 SRVs: ReflectionRayHit, ReflectionRayColor (albedo), ReflectionRayMaterial, and ReflectionRayEmission for reflection evaluation and debug paths
- 1 SRV: ReflectionEvaluatedRadiance for LightPass contribution composition and radiance debug

## Resource States

- Reads: depth + normal GBuffer + PBR params GBuffer as `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`
- Writes: reflection hit/albedo/material/emission payload outputs as `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`

## Render Graph Placement

```
GBufferPass
  -> RayQueryShadowPass (if shadow enabled)
  -> HybridReflectionPass (NEW, if raytracing supported && reflections enabled)
  -> LightingPass (consumes reflection output)
```

In `AddSceneRenderPasses()`, after `MakeRayQueryShadowPass()` block:

```cpp
if (m_rayQueryReflectionsEnabled)
{
    AddPass(MakeHybridReflectionPass());
}
```

Declare read/write resources via `.Reads()` / `.Writes()` for proper state tracking.

## What To Reuse

- **From RayQueryShadowPass**: Full-screen dispatch pattern, root signature layout (descriptor-table-based), render graph integration (Reads/Writes/UAV state), 8x8 thread group pattern, and descriptor/CBV binding conventions.
- **From SpecularDebugRayQueryPass**: Reflection ray setup logic (normal-offset origin, ray direction), hit-distance/candidate-location decode, and the `q.CommittedStatus()` / `q.CommittedRayT()` query pattern.

## TLAS SRV

Share the same TLAS SRV used by all other RayQuery passes:
`m_accelerationStructures.tlasSrv.Gpu()` bound at root param 1 (register t0, space0).
