# RTX 3090 resolution sweep

2026-11-17. Same Release binary and RTX 3090, driver 32.0.16.2124. Static 121 DamagedHelmets, DEFAULT vertex/index heaps, instancing OFF, raster five-pass mode, VSync/debug/UI off. Aspect ratio 16:9; camera and scene settings match across all nine reports. Each process has 600 warmup + 600 measurement frames. Three runs per resolution, order ascending/descending/ascending. No clock locking. Values below are medians of per-run averages in ms.

| Resolution | GPU Total | Depth | GBuffer | Light | ToneMap | GPU Total run range |
|---|---:|---:|---:|---:|---:|---:|
| 960x540 | 0.427423 | 0.111136 | 0.267044 | 0.038827 | 0.006223 | 0.425540-0.427505 |
| 1280x720 | 0.579470 | 0.115043 | 0.386144 | 0.060768 | 0.010805 | 0.577826-0.584878 |
| 1920x1080 | 0.850584 | 0.120016 | 0.576002 | 0.116659 | 0.024221 | 0.844484-0.865271 |

1080p has four times as many pixels as 540p, while GBuffer time grows by 2.16x. Depth PrePass changes much less. Thus the GBuffer cost has a material resolution-dependent component as well as a nontrivial fixed/geometry component. This does not distinguish pixel ALU, texture sampling, MRT writes, overdraw or small-triangle rasterization/quad efficiency. It is not proof that vertex work is free.

Next useful experiment: isolate pixel-shader/material work from MRT traffic while holding geometry and attachment formats fixed. Compare a diagnostic constant-output GBuffer PS with the normal PS, keeping the experiment out of the default production path. If the difference is large, investigate material sampling/normal-map/sRGB work; otherwise investigate MRT traffic and rasterization geometry. Avoid claiming a bandwidth bottleneck from the resolution sweep alone.

Validation: all nine runner executions completed normally and passed exact-five-pass, sample count, range, dimensions, heap and instancing checks. Executable SHA256: 0BE675DF43240BC26760FA7AF95E7631B51C98A7837310F597064231069E6AB6. Source C++ was unchanged during this sweep; the runner now accepts -Width/-Height and validates actual render and output dimensions. Raw reports remain ignored under bin/Benchmark/resolution-sweep/.
