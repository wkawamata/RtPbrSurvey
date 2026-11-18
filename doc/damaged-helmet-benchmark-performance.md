# DamagedHelmet Grid Benchmark performance investigation

Date: 2026-11-16
Workspace: C:\work\RtPbrSurvey-work-5
Source revision: b36e27a plus the DamagedHelmet initial display-count adjustment.

## Current evidence

- Release x64 MSBuild succeeded. This workspace initially lacked NuGet packages and vcpkg include/library configuration.
- Reused the existing local NuGet packages and vcpkg installation. Streamline SDK: C:\work\third_party\streamline-sdk-2.12.0.
- No performance timings have been collected. Host processes run, but the Computer Use session cannot enumerate their windows. Do not treat this as a measured rendering failure.
- No cause of the reported slowdown is confirmed yet.

## Code findings

- The DamagedHelmet Grid Benchmark initial display count was reduced from 1,000 to 121 in App/RtPbrSurveyApp.cpp. The maximum remains 1,000, and saved scene configuration may override the initial count. Other Grid Benchmark assets keep their existing defaults. The CLI -AutoSelectGltfDamagedHelmet selects the single-object viewer, not this benchmark.
- The asset has 46,356 indices, or 15,452 triangles per instance: 15,452,000 submitted triangles for 1,000 instances per geometry pass.
- RecordSceneGeometryDraw issues a separate draw with instance count 1 for every instance (Renderer/SceneGeometryPass.cpp). MakeSceneGeometryDrawDesc does not perform camera-frustum culling; it filters invalid transforms and mesh ranges.
- Raster rendering includes a DepthPrepass followed by the selected scene rendering passes (Engine/RtPbrSurveyEnginePasses.cpp). Geometry workload is therefore not limited to GBuffer.
- GBuffer writes seven render targets, totaling 36 bytes per pixel before depth, compression, caching and overdraw (Renderer/GBuffer.h). Its pixel shader samples material textures and evaluates normal mapping, motion vectors and sRGB conversion (Shaders/shaders_GBuffer.hlsl).
- The GBuffer vertex shader evaluates normal transformation cofactors and handedness per vertex (Shaders/SurfaceTransform.hlsli). This is a candidate to profile, not evidence of the primary bottleneck.
- GBuffer is compiled by DXC with the same command in Debug and Release, without -Od. Platform/ShaderCompiler.h disables optimization in Debug for its legacy compile helper, but that helper is not the GBuffer bytecode path.
- Current GPU timing labels use render-graph names such as GBufferPass, not DeferredShader. GPU row values are differences between adjacent timestamps; they are not isolated shader instruction timings.

## Measurement procedure

1. Run only one RtPbrSurvey process. Use the Release executable from this workspace. Record adapter, driver, revision, client/render resolution and graphics options.
2. Select glTF Grid Benchmark > DamagedHelmet > Load Scene. Do not substitute glTF Viewer > DamagedHelmet.
3. Use Deferred and the Lit output. Disable DLSS SR/RR, Hybrid Reflection and other optional ray effects for the initial raster baseline; record every changed option. Also retain a measurement of the original slow configuration once it is known.
4. Freeze animation and keep the same camera, mesh scale, lighting and UI visibility. Disable VSync for throughput measurements. Warm up for at least 120 frames, then sample each case for about 10 seconds, three times.
5. Record wall-clock FPS, CPU Frame, GPU Total, DepthPrepass and GBufferPass. Also record LightPass and any unexpectedly expensive pass. CPU FPS (1000 / CPU Frame) is not presentation FPS.
6. First compare instance counts 1, 100 and 1,000 at 1920x1080. Then compare 1280x720 versus 1920x1080 at 1,000 instances. Keep the camera/aspect ratio fixed and record actual render resolution.
7. Compare Forward with Deferred at 1,000 instances under matching lighting/options. Interpret the sum of relevant passes, since the work is divided differently.
8. If reproducing Debug-only slowness, compare Debug with and without the D3D12 debug layer separately. Use Debug for correctness checks; keep that evidence separate from Release throughput.

| Case | Resolution | Instances | CPU ms | GPU total ms | DepthPrepass ms | GBufferPass ms | Wall FPS |
|---|---|---:|---:|---:|---:|---:|---:|
| D1 | 1920x1080 | 1 | pending | pending | pending | pending | pending |
| D100 | 1920x1080 | 100 | pending | pending | pending | pending | pending |
| D1000 | 1920x1080 | 1000 | pending | pending | pending | pending | pending |
| D720 | 1280x720 | 1000 | pending | pending | pending | pending | pending |

## Interpretation

- Strong instance-count dependence with weak resolution dependence suggests geometry processing, tiny triangles or draw submission; distinguish CPU time from GPU geometry time.
- Strong resolution dependence suggests pixel shading, render-target traffic or overdraw; use a GPU profiler before selecting an optimization.
- A slow GPU total dominated by another pass does not establish a GBuffer shader problem.
- Simultaneous applications, camera changes, GPU clock changes and debug validation can invalidate comparisons.
- Candidate follow-ups after measurements: actual instancing for compatible mesh ranges, frustum culling, precomputed instance normal transforms, and GBuffer/pixel shader simplification. None is implemented or claimed to improve performance here.
## 2026-11-17: 120-frame average baseline and optimization priorities

All supplied runs use 1920x1080 render/output resolution. These replace the earlier screenshots as the quantitative baseline. GPU samples are latest-completed readbacks per UI frame; CPU/GPU samples are not guaranteed to identify the same submitted frame. Hardware/driver and exact settings still need recording.

| Instances | CPU Frame ms | Wall Frame ms | GPU Total ms | GBuffer ms | ToneMap ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 4.818459 | 5.552592 | 1.575151 | 0.052173 | 0.201532 |
| 10 | 12.612508 | 13.358718 | 3.453961 | 1.220582 | 0.265609 |
| 30 | 33.030419 | 33.759126 | 7.466667 | 4.466398 | 0.304341 |
| 60 | 66.568431 | 67.360458 | 10.395955 | 7.356629 | 0.188203 |
| 121 | 131.467353 | 132.525832 | 26.268459 | 19.216649 | 0.314402 |

### CPU diagnosis before optimization

RunFrame starts the CPU WorkMeter before UpdateFrame and stops it after RenderFrame. RenderFrame includes command recording/submission, Present and MoveToNextFrame fence waits. CPU Frame is therefore elapsed renderer time including waits, not exclusive CPU execution. Subtracting GPU Total from CPU Frame does not isolate CPU overhead.

The existing RendererCpuTimer already separates Update / housekeeping, Record / submit, GPU fence wait and Present. Added those values to WorkMeter OutputAverage. Completed() previously exposed the current accumulator, which Begin() clears before UI rendering; it now retains the last completed frame. A deterministic test verifies exclusive phase totals and retention across the next Begin(). Release build and the timing test pass. No renderer throughput improvement is claimed from this diagnostic change.

Measure 10 and 121 instances again. If Update dominates, profile scene updates and state comparisons; if Record / submit dominates, profile draw submission, UI and driver work; if GPU wait or Present dominates, inspect synchronization and presentation before optimizing CPU loops. The phase timer starts slightly earlier than the legacy CPU WorkMeter, so totals need not match exactly.

### Ranked GPU experiments (not implemented)

1. Static geometry in DEFAULT heaps. CreateMeshBuffers currently stores both vertices and indices in UPLOAD heaps. These are reused for every instance and geometry pass. Compare a DEFAULT-heap copy initialized once with correct copy/vertex/index/ray-tracing resource states and fence lifetime. This is a concrete memory-placement candidate, not a confirmed cause.
2. Geometry instancing. RecordSceneGeometryDraw emits one draw per instance even for shared mesh ranges. Batch compatible ranges while preserving instance offsets, per-instance materials, motion vectors and ObjectId. Measure Record / submit and GPU GBuffer together.
3. Camera-frustum culling for raster geometry. MakeSceneGeometryDrawDesc currently filters invalid transforms/ranges but not offscreen bounds. Retain offscreen objects in ray-tracing structures when they can cast shadows or appear in reflections; do not reuse a camera-visible list blindly for TLAS.
4. Vertex-shader work. GBuffer VS builds worldViewProj and computes normal cofactors/handedness per vertex. Evaluate per-instance precomputation while retaining nonuniform/negative scale correctness and motion-vector behavior. Depth PrePass and GBuffer timings differ too much to attribute all GBuffer cost to triangle submission alone.
5. GBuffer pixel/attachment bandwidth. Seven MRTs total 36 bytes/pixel before depth/compression. Compare 1280x720 with 1920x1080, holding camera/options fixed, before changing attachment precision or normal/color encoding. Such changes require image/normal/motion-vector validation.
6. TLAS updates. RebuildTlas uses PREFER_FAST_TRACE without an update/refit path. After profiling, consider dirty checks for static transforms or ALLOW_UPDATE/refit for animation, with explicit rebuilds when instance topology changes. These costs are not currently a separate GPU WorkMeter row, so do not assume they are already represented by RayQueryShadowPass alone.

ToneMap averages 0.188203 to 0.314402 ms and is a low priority for this baseline. GBuffer accounts for about 73.2% of GPU Total at 121 instances. The first CPU-phase measurement is required before claiming that any GPU optimization will remove the observed 132.5 ms wall frame.

The emissive-triangle table update is guarded by RenderingPath::PathTracing in UpdateFrame; it is not a Deferred CPU optimization target for this baseline. Imported-volume guards return early when no imported-volume material is present. Grid scene simulation updates its maximum instance array even with fewer displayed objects, so it alone does not explain the observed near-linear dependence on displayed count.