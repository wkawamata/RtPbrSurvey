# RTX 3090 Hybrid baseline and revised investigation

2026-11-17. DEFAULT geometry, instancing OFF, full GBuffer PS, static DamagedHelmet Grid, 1920x1080, Release, VSync/debug/UI off. Hybrid mode includes ray-traced shadows/reflections and per-frame acceleration-structure work. Three independent processes per instance count, 120 warmup + 120 measured frames each. Values are medians of per-run average milliseconds. Clocks and background system load are not locked.

| Instances | CPU Frame | Record / submit | GPU fence wait | GPU Total | GBuffer | CPU run range |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1.698 | 1.468 | 0.000 | 0.378 | 0.018 | 1.661-1.704 |
| 10 | 11.162 | 10.911 | 0.000 | 3.427 | 0.925 | 11.145-11.220 |
| 30 | 32.151 | 31.890 | 0.000 | 9.310 | 3.377 | 32.082-32.351 |
| 60 | 64.872 | 64.565 | 0.000 | 18.020 | 6.935 | 63.162-81.716 |
| 121 | 126.705 | 126.388 | 0.000 | 39.938 | 16.779 | 126.676-131.661 |

## Correctness checkpoint

121-instance raster output with instancing OFF and ON was captured at frame 30 during warmup; the PNG SHA256 values are identical. Capture occurs before measurement; exit-after-capture is not permitted with benchmark automation. The capture/report pair exits normally for both modes. This closes the missing multi-instance image-equivalence check from the instancing report.

PNG SHA256: 125883fd7804fa2027ce191a381554ff8e7e0f14ea0e5f34ebfcbb9bc5cb6178

## Interpretation and next target

CPU Record / submit dominates the current Hybrid frame; GPU fence wait is essentially zero. Do not transfer the raster-only conclusion (GPU-bound) to Hybrid. The 60-instance third run has elevated CPU/GPU times; ranges above preserve that variability. Old RTX 2080 data is not used for comparisons. The earlier 600-frame pilot under bin/Benchmark/rtx3090-hybrid-baseline was interrupted and is not mixed into this complete 120-frame series.

Code inspection found this unconditional chain in Hybrid: PopulateCommandList -> RebuildAccelerationStructures -> NrdRelaxGeometrySnapshot::RecordTlas -> BuildNrdRelaxWorldBounds. RecordTlas rebuilds bounds by visiting each instance and every referenced vertex/index, even in this non-PT Hybrid benchmark. Its O(instance count * index count) work is consistent with observed scaling. This is a strong candidate, not yet an isolated measured cause. Next add a scoped timing around this call before changing caching or gating. Preserve NRD snapshot validity when optimizing; do not simply skip validation in paths that need it.

Additional timing caveat: GPU StartGpu is issued in BeginFrame, before TLAS rebuild. The GPU interval labelled Clear can include pre-pass acceleration-structure work and barriers; it is not isolated Clear shader time. A dedicated TLAS timestamp is warranted when profiling that stage.

Do not add more GBuffer feature toggles until this CPU bottleneck is resolved. Retain verified DEFAULT placement and optional instancing. No new rendering optimization was made in this step. Raw JSON/CSV and environment metadata remain ignored under bin/Benchmark/rtx3090-hybrid-baseline-120/.
