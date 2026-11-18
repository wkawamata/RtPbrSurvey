# Automated DamagedHelmet benchmark

Run from the repository root with a Release x64 build:

```powershell
.\Tests\Invoke-HelmetBenchmark.ps1 -Mode raster -Repeats 5
```

The runner tests 1, 10, 30, 60 and 121 instances sequentially. Each fresh process
warms up for 120 rendered frames and measures the following 120 frames. Reports
are stored under ignored `bin/Benchmark/<timestamp>/`: one JSON per run,
`summary.csv`, and `environment.json` with source revision, working-tree status,
executable hash and driver information when available. Existing reports are not
overwritten by the runner. Other RtPbrSurvey processes must be closed first.

For a single case:

```powershell
.\bin\x64\Release\RtPbrSurvey.exe -BenchmarkReport C:\work\RtPbrSurvey-work-5\bin\Benchmark\single.json -BenchmarkInstances 121 -BenchmarkWarmupFrames 120 -BenchmarkFrames 120 -WindowSize 1920 1080 -DisableVSync -DisableD3D12DebugLayer
```

`-BenchmarkReport` selects the DamagedHelmet **Grid Benchmark**, restores scene
defaults instead of user overrides, sets the requested instance count, freezes
simulation, selects Deferred / LightPass. The default `raster` mode disables ray-traced shadows, reflection, diffuse environment visibility, DLSS SR/RR and acceleration-structure builds/updates. Its measured graph is exactly Clear, Depth PrePass, GBufferPass, LightPass and ToneMapPass; DebugLine and ImGui passes are omitted. IBL and ordinary direct lighting remain enabled according to the recorded settings. Use `-Mode hybrid` in the runner or `-BenchmarkMode hybrid` on the CLI to retain the earlier Hybrid Reflection configuration. UI is
hidden. The complete effective scene configuration and adapter description are
saved in the report. A requested render preset can provide additional settings;
the benchmark's scene count, static pose and selected raster/hybrid mode take precedence.

Metrics contain arithmetic means in milliseconds, sample counts, minima and
maxima. CPU phases separate Update / housekeeping, Record / submit, GPU fence
wait and Present. CPU Frame includes waits and must not be read as exclusive CPU
execution time. GPU samples use the most recently completed frame after each
rendered frame; readback is delayed relative to CPU samples. Do not interpret the
CPU/GPU difference as a directly measured CPU-only cost. FPS can be calculated
from mean Wall Frame time; no average-of-instantaneous-FPS is reported.

The report is atomically finalized before the application exits. A write failure
sets a nonzero exit code. The runner enforces a five-minute per-case timeout and
checks exit status, report existence, sample counts and metric ranges. Warmup
frames may be zero; measurement frames must be positive. Instance counts must be
in [1, 1000]. No clock/power locking is applied, so use repeats and compare the
same hardware, camera, render configuration and UI policy. Hidden-UI runs are a
new baseline and should not be equated with earlier interactive WorkMeter runs.

On machines with Streamline, launching inside the agent sandbox may fail before
initialization. Run the unchanged executable with normal host permissions rather
than disabling the SDK. Reports and logs are generated artifacts; do not commit
them.

## Geometry and instancing

Static vertex/index buffers always use DEFAULT heaps. Upload staging is retained until the scene-upload fence completes. The legacy direct-UPLOAD geometry path and its CLI/runner options have been removed; historical A/B results remain in doc/rtx3090-geometry-heap-ab.md.

Use -Instancing on or off in the runner (default off), or -BenchmarkInstancing on/off on the executable. The report records instancing. Compatible contiguous instances are batched without reordering; gaps and different geometry ranges split batches. Per-instance materials, transforms, previous transforms and ObjectId use the original instance-buffer indices.

## GBuffer PS diagnostic

-GBuffer full/constant selects the normal or constant-output GBuffer pixel shader (executable: -BenchmarkGBuffer full/constant). Constant mode is restricted to raster benchmark automation and intentionally changes the rendered image. It preserves VS bytecode, topology, seven MRT formats and depth state, but removes material/texture/motion calculations. Constant values can improve render-target compression and remove interpolator demand, so the delta is not an isolated ALU or bandwidth measurement. Compare GBufferPass rather than downstream LightPass or GPU Total, whose inputs also change.

## Repetition and Depth PrePass

Default repetitions are now 5. summary.csv retains each run; averages.csv contains the arithmetic mean and range of per-run averages. Each run still measures Frames rendered frames after WarmupFrames. Debug runs are correctness evidence only.

-DepthPrepass on/off controls the benchmark's depth prepass (executable: -BenchmarkDepthPrepass on/off). Default on. OFF is restricted to benchmark automation and enables GBuffer depth writes; graph depth ownership changes accordingly. No ordinary rendering default is changed. Compare total GPU time and the sum of Depth PrePass plus GBuffer, not either pass in isolation.

## Draw order and pipeline statistics

-DrawOrder forward/reverse (executable: -BenchmarkDrawOrder) uses the Grid's existing Z-sorted order or reverses the final draw list without changing instance IDs, materials, transforms, TLAS order or ObjectId. With the default static camera these correspond to near-to-far and far-to-near. Reverse mode requires instancing OFF and benchmark automation.

All benchmark runs now query D3D12 PIPELINE_STATISTICS around GBufferPass. Results are resolved into per-frame readback resources and consumed after the existing frame-resource fence wait. gbufferPipelineStatistics contains samples and averagePsInvocations (a count, not milliseconds or unique pixels). The same delayed completed-frame semantics apply as GPU timestamps. Query overhead is present equally in all comparison conditions; compare fresh same-binary runs. summary.csv includes the counts and averages.csv aggregates the five run averages.
