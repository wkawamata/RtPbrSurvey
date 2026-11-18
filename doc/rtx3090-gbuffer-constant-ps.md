# GBuffer constant-output PS diagnostic

2026-11-17. RTX 3090, driver 32.0.16.2124. Same Release binary, 1920x1080, static 121 Helmets, instancing OFF, DEFAULT vertex/index buffers, raster five-pass mode, VSync/debug/UI off. Three independent runs per shader, 600 warmup + 600 measured frames per run, ordered full/constant, constant/full, full/constant. No clock locking. Table values are medians of per-run average milliseconds.

| Pass | Full PS | Constant PS |
|---|---:|---:|
| GPU Total | 0.848872 | 0.476488 |
| GPU[2] Depth PrePass | 0.119936 | 0.108421 |
| GPU[3] GBufferPass | 0.575857 | 0.272640 |
| GPU[4] LightPass | 0.114756 | 0.064200 |
| GPU[5] ToneMapPass | 0.024404 | 0.017609 |

GBuffer drops by 52.7% in this diagnostic. Same VS bytecode, draw stream, topology, seven MRT formats and depth state are used. PSConstant removes material loads, texture samples, sRGB conversion, normal-map and motion-vector calculations, writing fixed values to all seven attachments.

This is not a visual-equivalent optimization: output is deliberately different. Constant attachments can compress better and unused PS inputs can reduce interpolator demand even with unchanged VS bytecode. The delta therefore does not measure pure ALU time or prove a texture/bandwidth bottleneck. LightPass and ToneMap also receive different content, so their timing or the GPU Total difference must not be claimed as production speedup.

The result supports further PS-side isolation before prioritizing vertex-only changes or attachment format changes. Next compare individual material features (normal mapping, sRGB conversion and texture sampling) with explicit diagnostic labels and unchanged MRT formats; validate any production simplification against reference images.

Implementation: -BenchmarkGBuffer full/constant, runner -GBuffer full/constant. Default full. Constant mode is rejected outside raster benchmark automation. Both MSBuild and CMake shader lists compile PSConstant. Reports record gbufferShader and runner checks it.

Validation: Release and Debug builds pass; Debug constant PS / 121 instances / 4 warmup + 8 measured frames exits 0 with D3D12 ERROR 0. All six Release reports pass frame/sample-count, five-pass, resolution and settings checks. Scene config and adapter are identical across the six reports. RendererCpuTiming and EvaluationState regression tests pass. No visual-equivalence assertion is applicable to the diagnostic.

Raw results: bin/Benchmark/gbuffer-ps-ab/{full,constant}-{1,2,3}/. Debug evidence: bin/Benchmark/gbuffer-ps-debug/. Generated files remain ignored.
