# RTX 3090 minimal Deferred baseline

Measured 2026-11-17. This is a new RTX 3090 baseline, not a before/after comparison with the older RTX 2080 interactive results.

Configuration: Release x64, NVIDIA GeForce RTX 3090, driver 32.0.16.2124, 1920x1080 render/output, static DamagedHelmet Grid, VSync off, debug layer off, UI hidden. Ray-traced shadows, Hybrid Reflection, diffuse environment visibility, DLSS SR/RR, acceleration-structure build/update, DebugLine and ImGui passes are excluded. Direct lighting and IBL remain; complete settings are in each JSON report.

Verified executed GPU passes:

Clear -> Depth PrePass -> GBufferPass -> LightPass -> ToneMapPass

## Results

Numbers below are medians of three independent per-process averages, in ms. Each 10/30/60/121 run uses 120 warmup + 120 measured frames. The original 1-instance runs varied from 0.450 to 0.940 ms GPU total; a follow-up uses 5000 warmup + 600 measured frames, and those follow-up values are used only for the 1-instance row. Warmup/measurement durations therefore differ for that row. GPU clocks/power are not locked; this is an exploratory baseline, not a controlled cross-GPU benchmark.

| Instances | CPU Frame | Wall Frame | GPU Total | GBuffer | ToneMap | GPU Total range across runs |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0.409 | 0.686 | 0.160 | 0.027 | 0.030 | 0.160-0.161 |
| 10 | 0.410 | 0.680 | 0.534 | 0.411 | 0.019 | 0.530-0.552 |
| 30 | 1.553 | 1.846 | 1.700 | 1.544 | 0.019 | 1.699-1.705 |
| 60 | 3.145 | 3.459 | 3.313 | 3.095 | 0.021 | 3.309-3.327 |
| 121 | 5.179 | 5.505 | 5.323 | 5.025 | 0.022 | 5.310-5.353 |

At 121 instances, GBuffer is 94.4% of GPU total. CPU fence wait is 4.592 ms, Record / submit 0.343 ms, Update / housekeeping 0.270 ms and Present 0.153 ms (each is a median of per-run averages, so their sum need not equal a median total). CPU phase timing also starts slightly earlier than legacy CPU Frame timing. The remaining CPU interval is predominantly fence wait; this baseline supports prioritizing GBuffer GPU work over ToneMap or a general CPU rewrite. It does not identify the shader/memory bottleneck within GBuffer.

Next isolated experiment: move static vertex/index buffers from UPLOAD to DEFAULT heaps, then rerun the same raster mode and check images/debug-layer correctness. Preserve instancing, shader and material behavior initially so the memory-placement effect can be evaluated separately.

## Evidence and checks

- All 15 initial reports and three extended 1-instance reports passed runner validation (exit code, frame count, sample counts, metric ranges and exact five-pass set).
- Debug x64: 121 instances, 4 warmup + 8 measured frames; exit 0, D3D12 ERROR 0. Debug timings are not included above.
- Renderer CPU timing and EvaluationState regression tests pass.
- Source revision: b36e27a85b2e7fe066ea2c6ad1a994ded8444097 plus uncommitted changes. Release executable SHA256: 9DB0EB8C390D3DF6680DCD0E4B3B9CE23A0EC8F08AA739D3949B4977FD642E8E.
- Raw reports: `bin/Benchmark/rtx3090-raster-baseline-20261117/` and `bin/Benchmark/rtx3090-raster-one-stability-20261117/`.
- Aggregate: `bin/Benchmark/rtx3090-raster-baseline-20261117/baseline-median.csv`. Raw artifacts remain ignored.
- Each process was run sequentially with no other RtPbrSurvey process active. No clock locking or exhaustive system-load control was applied.
