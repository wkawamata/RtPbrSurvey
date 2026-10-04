# Path Tracing Reference Audit

## 1. Purpose

This audit maps selected path-tracing reference implementations to RtPbrSurvey without copying their framework or GPU
pipeline architecture. It records source locations, transferable concepts, known differences, and the order in which
the references should influence implementation.

The first implementation target remains a compute shader using Inline RayQuery. Full DXR shaders, shader tables,
external denoisers, ReSTIR, CUDA, and neural caches are later stages.

## 2. Local reference checkouts

Reference repositories are shallow clones stored outside the RtPbrSurvey repository.

Root: `C:\work\RtPbrSurvey-references`

| Project | Local directory | Audited revision | Role |
|---|---|---|---|
| MJP DXRPathTracer | `DXRPathTracer` | `0af23bd` | minimal progressive PT concepts |
| ZetaRay | `ZetaRay` | `6fd82f1` | Inline RayQuery, NEE, ReSTIR and temporal reuse |
| Real-Time-Path-Tracer | `Real-Time-Path-Tracer` | `9ccd14a` | NRD signals and host integration |
| RoyalTracer-DX | `RoyalTracer-DX` | `9e9130a` | ReSTIR PT, DLSS RR, NRC and DX12/CUDA boundary |

RoyalTracer-DX declares `Pathtracer/third_party/tiny-cuda-nn` as a submodule. It is intentionally not initialized for
this audit. The checked-in NRC bridge and layout files are sufficient to study the boundary.

## 3. MJP DXRPathTracer

### 3.1 Primary files

All paths below are relative to `C:\work\RtPbrSurvey-references\DXRPathTracer\DXRPathTracer`.

| Concern | Source location | Observation |
|---|---|---|
| primary ray | `RayTrace.hlsl:92` `RaygenShader()` | jittered pixel sample is unprojected with inverse view-projection |
| sample sequence | `RayTrace.hlsl:85` `SamplePoint()` | CMJ sample keyed by pixel, sample index and sample-set index |
| radiance payload | `RayTrace.hlsl:63` `PrimaryPayload` | radiance, roughness, path length and sampling state |
| shadow payload | `RayTrace.hlsl:73` `ShadowPayload` | binary visibility |
| surface/material shading | `RayTrace.hlsl:151` `PathTrace()` | emission, direct lights, BRDF sampling and recursive continuation |
| diffuse sampling | `RayTrace.hlsl:327` | cosine-weighted hemisphere; throughput reduces to diffuse albedo |
| specular sampling | `RayTrace.hlsl:338` | GGX visible-normal sampling and masking-shadowing term |
| lobe probability | `RayTrace.hlsl:321` | diffuse/specular selected at 0.5 and compensated by multiplying throughput by 2 |
| recursive continuation | `RayTrace.hlsl:388` | child radiance is multiplied by local throughput on unwind |
| environment/miss | `RayTrace.hlsl:509` `MissShader()` | sky or white-furnace radiance; visible sun on primary miss |
| hit reconstruction | `RayTrace.hlsl:443` `GetHitSurface()` | geometry offsets, triangle indices and barycentric interpolation |
| material lookup | `RayTrace.hlsl:466` `GetGeometryMaterial()` | material is assigned per geometry |
| alpha test | `RayTrace.hlsl:485`, `RayTrace.hlsl:497` | separate any-hit shaders ignore transparent hits |
| accumulation | `RayTrace.hlsl:142` | in-place running average using current sample index |
| dispatch | `DXRPathTracer.cpp:1313` `RenderRayTracing()` | full DXR `DispatchRays` and one sample-index increment per dispatch |
| reset detection | `DXRPathTracer.cpp:931-985` | scene, camera and rendering-setting changes reset sample index |
| PSO/payload sizing | `DXRPathTracer.cpp:777` | payload size declared in state-object shader config |
| recursion depth | `DXRPathTracer.cpp:791` | state-object recursion depth follows maximum path length setting |

### 3.2 Correction: no Russian Roulette

The audited MJP revision does not use Russian Roulette. It terminates paths through `MaxPathLength`. Therefore:

- MJP is the reference for primary ray, payload, BRDF sampling, throughput, emission, miss and accumulation.
- ZetaRay is the first implementation reference for Russian Roulette.
- RtPbrSurvey does not add Russian Roulette until fixed-depth direct/diffuse/specular transport is validated.

### 3.3 Full DXR to Inline RayQuery mapping

| MJP full DXR | RtPbrSurvey initial implementation |
|---|---|
| `RaygenShader()` | `CSMain()` invocation for one pixel |
| `PrimaryPayload` | local `PathState` owned by the compute invocation |
| recursive `TraceRay()` | bounded iterative bounce loop and `RayQuery` per bounce |
| `ClosestHitShader()` | committed-hit reconstruction followed by an explicit shading call |
| `MissShader()` | branch after `CommittedStatus()` |
| `ShadowPayload` | boolean returned by an inline visibility query |
| recursion unwind | update `radiance += throughput * contribution` in the loop |
| `DispatchRays()` | `Dispatch(ceil(width/8), ceil(height/8), 1)` |
| shader table | not used |

The initial `PathState` should contain at least:

```hlsl
struct PathState
{
    float3 rayOrigin;
    float3 rayDirection;
    float3 radiance;
    float3 throughput;
    uint bounceIndex;
    uint rngState;
    bool previousBounceWasSpecular;
};
```

The exact HLSL representation may avoid `bool` for layout clarity. This is local shader state and does not need a CPU
mirror.

### 3.4 Concepts to adopt

- jitter primary samples inside each pixel
- keep deterministic sample identity per pixel
- accumulate only linear HDR radiance
- reset accumulation on camera, scene and estimator changes
- add emission before continuing the path
- separate radiance and visibility queries conceptually
- preserve the PDF/probability compensation when selecting a BRDF lobe
- include a white-furnace validation mode before claiming PBR correctness

### 3.5 Concepts not to copy

- full DXR state object and shader-table structure
- recursive path tracing
- framework-global descriptor heap conventions
- fixed 50/50 diffuse/specular selection as the final sampling policy
- FP16 clamp before a 32-bit accumulation sum
- all-light loops as the final many-light solution
- alpha-test any-hit behavior in the opaque-only first milestone

MJP's in-place running average is useful as a reference but RtPbrSurvey keeps the design choice of an
`R32G32B32A32_FLOAT` radiance sum and a separate normalized HDR output. This gives later variance/debug operations a
less ambiguous source.

## 4. ZetaRay

### 4.1 Primary files

Paths are relative to `C:\work\RtPbrSurvey-references\ZetaRay\Source\ZetaRenderPass`.

| Concern | Source location | Use in RtPbrSurvey |
|---|---|---|
| baseline indirect PT | `IndirectLighting/PathTracer/PathTracer.hlsl` | iterative RayQuery/path structure after minimal PT |
| PT constants | `IndirectLighting/PathTracer/Params.hlsli` | RR threshold and NEE/MIS policy reference |
| reusable path loop | `IndirectLighting/ReSTIR_GI/PathTracing.hlsli` | explicit BSDF/PDF/path state reference |
| NEE | `IndirectLighting/NEE.hlsli` | sun, sky and emissive-light estimate separation |
| RayQuery helper | `Common/RayQuery.hlsli` | visibility and closest-hit API shape |
| host integrator switch | `IndirectLighting/IndirectLighting.cpp:220` | Native PT, ReSTIR GI and ReSTIR PT remain selectable |
| native PT dispatch | `IndirectLighting/IndirectLighting.cpp:237` | pass timing and dispatch boundary |
| temporal reset | `IndirectLighting/IndirectLighting.cpp:203-215` | resize/reset invalidates reservoirs and ping-pong index |
| ReSTIR GI host path | `IndirectLighting/IndirectLighting.cpp:277` | resource transitions and temporal reservoir ownership |
| ReSTIR PT temporal | `IndirectLighting/IndirectLighting.cpp:370` | sort/reconnect/replay decomposition |
| ReSTIR PT reservoirs | `IndirectLighting/ReSTIR_PT/Reservoir.hlsli` | reservoir fields and update rules |
| ReSTIR PT initial path | `IndirectLighting/ReSTIR_PT/ReSTIR_PT_PathTrace.hlsl` | path recording before reuse |
| ReSTIR PT temporal replay | `IndirectLighting/ReSTIR_PT/ReSTIR_PT_Replay.hlsl` | temporal shift/replay stage |
| ReSTIR PT spatial search | `IndirectLighting/ReSTIR_PT/ReSTIR_PT_SpatialSearch.hlsl` | neighbor selection |
| emissive ReSTIR DI | `DirectLighting/Emissive/` | many-light candidate and reuse reference |
| sky ReSTIR DI | `DirectLighting/Sky/` | environment direct-light reuse reference |

### 4.2 Immediate findings

- `PathTracer.hlsl` starts from a raster GBuffer primary surface rather than tracing the primary visibility ray. It is
  therefore not the reference for RtPbrSurvey's initial camera-ray/hit stage.
- It explicitly carries BSDF PDF data and calls a reusable path-trace implementation. This is valuable after the
  primary-hit milestone.
- `Params.hlsli` starts Russian Roulette after three bounces.
- NEE distinguishes sun, sky and emissive estimates and records both solid-angle and light PDFs.
- Native Path Tracing disables temporal reservoir reuse, while ReSTIR methods own explicit temporal-valid state.

### 4.3 Adoption boundary

Adopt the following order, never as one large port:

1. explicit BSDF sample result with `wi`, `f/pdf`, PDF and lobe identity
2. NEE for the existing directional light
3. environment and emissive sampling
4. Russian Roulette after a configurable minimum depth
5. denoiser-ready primary-surface/motion outputs
6. ReSTIR DI with temporal reuse disabled by default
7. temporal reuse, then spatial reuse
8. separate ReSTIR GI versus ReSTIR PT evaluation

Do not copy ZetaRay's descriptor-heap indexing, GBuffer layout, render framework, thread-group swizzle or transmissive
material model into the first implementation.

## 5. Real-Time-Path-Tracer

### 5.1 Primary files

Paths are relative to `C:\work\RtPbrSurvey-references\Real-Time-Path-Tracer\PathTracer`.

| Concern | Source location | Use in RtPbrSurvey |
|---|---|---|
| ray-generation outputs | `res/shaders/raytracing/ray_gen.hlsl` | signal and guide-buffer write locations |
| primary material/hit | `res/shaders/raytracing/hit.hlsl` | normal, roughness, albedo, diffuse/specular and hit distance |
| miss behavior | `res/shaders/raytracing/miss.hlsl` | signal defaults on miss |
| indirect payload | `res/shaders/raytracing/indirect.hlsl` | separated indirect radiance/hit data |
| payload/data contracts | `res/shaders/raytracing/common.hlsli` | CPU/GPU signal layout reference |
| path/BRDF utilities | `res/shaders/raytracing/path_tracing_utils.hlsli` | utility survey only; do not copy estimator wholesale |
| NRD front-end include | `res/shaders/raytracing/include/NRD.hlsli` | official packing helper integration point |
| motion-vector shaders | `res/shaders/mv_vs.hlsl`, `mv_ps.hlsl` | convention comparison |
| host NRD dispatch | `src/rendering/RaytracingRenderer.cpp:949-1127` | permanent/transient pools and resource mapping |
| composite | `src/rendering/RaytracingRenderer.cpp:1130` | denoised signal composition |
| NRD settings | `src/rendering/RaytracingRenderer.cpp:1497` | matrix, jitter and motion-vector scale contract |

### 5.2 Adoption boundary

Use this project to enumerate NRD resources, packing helpers, matrix conventions and signal composition. Do not use it
as the minimal PT architecture: it is a full DXR renderer with broad host-side ownership and multiple intertwined
features.

Before NRD integration, every proposed input must be shown independently in RtPbrSurvey Debug Texture Preview:

- motion vectors
- view depth
- normal and roughness
- diffuse radiance and hit distance
- specular radiance and hit distance
- optional shadow/translucency signal

The NRD resource table and encoding are confirmed against the NRD version selected for RtPbrSurvey, not assumed from
this repository's pinned revision.

## 6. RoyalTracer-DX

### 6.1 Primary files

Paths are relative to `C:\work\RtPbrSurvey-references\RoyalTracer-DX\Pathtracer`.

| Concern | Source location | Use in RtPbrSurvey |
|---|---|---|
| path state | `shaders/Path_State_v8.hlsli` | advanced state layout survey |
| path sampling | `shaders/Path_Sampler_v8.hlsli` | lobe/path sampling survey |
| camera ray | `shaders/Camera_Ray_v8.hlsli` | primary ray convention comparison |
| BSDF | `shaders/BXDF_v8.hlsli` and `Material_*_v8.hlsli` | layered model, later PBR reference |
| inline ray tracing | `shaders/Inline_RT_v8.hlsli` | guarded RayQuery and hit-object helpers |
| ray-generation pass | `shaders/Pass_raygen_v8.hlsl` | path/reservoir/DLSS signal production |
| reservoir layout | `shaders/Reservoir_v8.hlsli` | ReSTIR PT storage survey |
| temporal/spatial reuse | `shaders/Pass_temp_gi_v8.hlsl`, `Pass_spat_gi_v8_1.hlsl` | pass decomposition |
| final shading | `shaders/Pass_shading_v8.hlsl` | reservoir-to-radiance composition |
| DLSS RR inputs | `shaders/Includes_v8.hlsli:185` and `Common_v8.hlsli` | RR auxiliary-resource reference |
| NRC shader layout | `shaders/Nrc_v8.hlsli` | byte-exact DX12/CUDA shared layout |
| NRC resolve | `shaders/Pass_nrc_resolve_v8.hlsl` | inference result composition |
| C++ shared layout | `rdn/NRC/NrcLayout.h` | HLSL/CUDA layout mirror |
| CUDA interop | `rdn/Interop/CudaInterop.h/.cpp` | external memory and fence boundary |
| network backend | `rdn/NRC/NrcNetwork.h`, `NrcNetwork.cu` | tiny-cuda-nn ownership and CUDA streams |
| host pass scheduling | `rdn/Renderer_Pipeline.cpp` | DX12, Streamline and `cuda:` pass ordering |

### 6.2 Adoption boundary

- Treat RoyalTracer-DX as an advanced architecture survey, not the starting implementation.
- Keep DLSS RR independent from Native accumulation and any future NRD backend.
- If NRC is pursued, mirror only a versioned neutral buffer contract in core code.
- CUDA resources, stream/event synchronization and tiny-cuda-nn stay in an optional backend or plugin DLL.
- A failure to initialize CUDA/NRC must not disable Native Path Tracing.
- Do not reserve large fixed descriptor ranges in the core renderer based on this reference.

## 7. Existing RtPbrSurvey reuse map

| Required PT function | Existing RtPbrSurvey source | Action |
|---|---|---|
| support query | `Renderer/RayTracingSupport.*` | reuse; require tier 1.1 for Inline RayQuery |
| BLAS/TLAS | `Renderer/AccelerationStructureResources.*` | reuse |
| vertex/index/range data | `RtPbrSurveyEngine::CreateSceneGeometryBuffers()` | reuse through `RayQuerySceneBindings` |
| per-frame instances | `RtPbrSurveyEngine` frame resources | reuse |
| material/texture lookup | `MaterialBuffer`, bindless texture table | reuse |
| committed-hit reconstruction | `Shaders/shaders_HybridReflection.hlsl:70-305` | behavior-preserving extraction |
| rough GGX sampling | `Shaders/ReflectionSampling.hlsli` | reuse math later; retain Hybrid RNG behavior |
| directional visibility | `Shaders/shaders_RayQueryShadow.hlsl` | reuse concept/bias convention, not pass input layout |
| camera inverse VP | camera constant buffer | reuse without DLSS temporal jitter in PT mode |
| resource lifecycle | `RenderTextureSpec` and resource registry | reuse |
| pass graph | `RtPbrSurveyEnginePasses.cpp` | add independent Path Tracing branch |
| display | ToneMap pass | reuse with `PathTracing.SceneColor` source |
| inspection | RenderGraph and Debug Texture Preview | register every PT output |
| state capture | `SceneRendererSettings` and Evaluation Cases | extend with PathTracingSettings |

## 8. Decisions for the first implementation

### 8.1 Estimator

- one path per pixel per sample
- one sample per pixel per frame by default
- deterministic per-path RNG
- opaque triangle geometry only
- fixed maximum depth initially
- accumulated linear HDR output
- no temporal reuse, denoiser, SR or RR

### 8.2 Minimal state

Keep explicit state even when the first diagnostic shader uses one bounce:

- ray origin and direction
- accumulated radiance
- throughput
- current bounce
- RNG state
- previous-lobe identity
- current/previous PDF fields when MIS work starts

This makes MJP's recursive contribution flow visible in the iterative implementation and avoids redesigning the loop
when diffuse indirect lighting is added.

### 8.3 Accumulation

- `PathTracing.Accumulation`: `R32G32B32A32_FLOAT` radiance sum
- `PathTracing.SceneColor`: `R16G16B16A16_FLOAT` normalized radiance
- CPU-side uniform accumulated sample count
- clear pass on invalid history
- no FP16 clamp before accumulation
- reset reason visible in UI

### 8.4 Validation modes

The first shader should expose these outputs before full lighting:

1. hit/miss mask
2. world normal
3. base color
4. metallic/roughness
5. emissive
6. primitive/instance/material identity hash

The identity outputs are needed to prove that shared Hybrid hit reconstruction still resolves the same geometry and
materials.

## 9. Phase 0 conclusions

- [x] Reference repositories pinned and located outside the product repository.
- [x] MJP primary-ray, payload, BRDF, throughput, emission, miss, accumulation and reset sources mapped.
- [x] MJP fixed-depth termination distinguished from Russian Roulette.
- [x] Full DXR concepts mapped to iterative Inline RayQuery state.
- [x] ZetaRay native PT, NEE, RR and ReSTIR entry points mapped.
- [x] Real-Time-Path-Tracer NRD signal and host resource entry points mapped.
- [x] RoyalTracer-DX ReSTIR PT, DLSS RR, NRC and CUDA boundary entry points mapped.
- [x] Existing RtPbrSurvey Hybrid code reuse boundary mapped.
- [ ] Implementation code started.

## 10. Next task

Implement Commit 1 from `path-tracing-design.md` on a new implementation branch created from the accepted design base:

- add `RenderingPath::PathTracing` at the end of the enum
- add `PathTracingSettings` and read-only runtime support status
- capture/apply/serialize settings with backward-compatible defaults
- add Path Tracing to the rendering-path UI
- keep GPU execution disabled and display the reason
- ensure DLSS SR/RR settings remain stored but are inactive in Path Tracing mode
- add focused serialization and rendering-path tests

No shader, resource, root-signature or pass changes belong in Commit 1.
