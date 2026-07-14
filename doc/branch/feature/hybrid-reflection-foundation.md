# HybridReflectionPass Design Note

## Overview

Recommendation for the shape of a full-screen HybridReflectionPass using RayQuery inline raytracing to compute specular reflections from the GBuffer.

Current implementation status: HybridReflectionPass scaffold is wired into the render graph and writes `ReflectionRayHit` as `.x = hit distance`, `.y = hit flag`, `.z/.w = oct-encoded hit attribute`. It writes `ReflectionRayColor.rgb` with hit material color: linear hit albedo plus hit emissive. It also writes `ReflectionRayMaterial` as `.x = metallic`, `.y = roughness`, `.z = unlit flag`, `.w = reserved`. These are material payloads, not reflected radiance.

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

**Current format: hit/miss + hit distance + hit normal**:

- `RWTexture2D<float4>` -- .x = hit distance (0 on miss), .y = hit flag (1.0 hit, 0.0 miss), .z/.w = oct-encoded hit normal.
- `RWTexture2D<float4>` color -- hit material color from linear hit albedo plus hit emissive. This is not reflected radiance; lighting should move to a later reflection evaluation step.
- `RWTexture2D<float4>` material -- hit material payload: metallic, roughness, unlit flag, reserved. This separates material parameters from color so the next reflection-evaluation step can use a BRDF-shaped payload instead of overloading `ReflectionRayColor`.
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
- `Reflection Contribution` lets LightPass consume `ReflectionRadiance`; LightPass applies the visible-surface Fresnel term before adding it.
- `ReflectionEvaluatePass` writes one-bounce hit-point radiance into `ReflectionRadiance`, including direct light, diffuse IBL, specular IBL approximation, emissive, miss fallback, distance fade, and visible-surface roughness fade.
- Reflection hit direct lighting now uses the shared PBR direct-light BRDF helper.
- Reflection hit diffuse/specular IBL uses the shared PBR IBL helpers, including BRDF LUT based specular IBL.
- Reflection hit shading is organized as `PbrSurface -> PbrRadianceComponents -> evaluated radiance` in shared HLSL helpers.
- `Reflection Radiance` debug view visualizes the current `ReflectionRadiance` texture with simple tone mapping.
- Reflection radiance component debug views visualize the direct, diffuse IBL, specular IBL, and emissive contributions recomputed from the hit payload.
- `Reflection Contribution Max Distance` fades the provisional contribution by hit distance, reducing far-hit color bleeding while the reflection color is still approximate.
- Material Gate is disabled by default: `maxRoughness = 1.0`, `minMetallic = 0.0`, preserving the initial "trace all visible pixels" behavior.
- When `Material Gate` is enabled in the Debug UI, the pass uses `HybridReflectionSettings::maxRoughness` and `minMetallic`.
- The first useful debug setting is roughness-focused (`maxRoughness = 0.35`, `minMetallic = 0.0`) so glossy dielectric and metallic surfaces can both be inspected.

## Remaining Reflection Radiance Work

- Decide whether the reflection hit path needs hit ambient occlusion or should keep `ambientOcclusion = 1.0` until a payload/source exists.
- Decide whether miss fallback should use the same roughness-aware prefiltered environment path as hit specular IBL.
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

- 3 UAVs: reflection hit, color, and material payload output textures
- 1 SRV: TLAS (`m_accelerationStructures.tlasSrv.Gpu()`, register t0)
- 1 SRV: depth buffer
- 1 SRV: GBuffer normal
- 1 SRV: GBuffer PBR params
- 1 CBV: camera constants
- 3 root SRVs: scene vertex/index/instance buffers for committed hit normal and materialId reconstruction
- 1 SRV: material buffer for hit material parameter debug
- 1 SRV table: scene texture table for hit albedo texture debug
- 1 SRV: ReflectionRayColor for LightPass hit-color overlay and future reflection contribution
- 1 SRV: ReflectionRayMaterial for future reflection evaluation

## Resource States

- Reads: depth + normal GBuffer + PBR params GBuffer as `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`
- Writes: reflection hit/color/material payload outputs as `D3D12_RESOURCE_STATE_UNORDERED_ACCESS`

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
