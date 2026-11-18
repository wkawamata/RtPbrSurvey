# Depth PrePass ON/OFF (five-run arithmetic means)

2026-11-18. RTX 3090, same Release binary, 1920x1080 Hybrid, static 1000 Helmets, DEFAULT geometry, instancing OFF, normal GBuffer PS, VSync/debug/UI off. Five independent runs each, 120 warmup + 120 measured frames per process. ON/OFF order alternates per pair. Equal sample counts: arithmetic mean of five per-run averages, not median. GPU clocks/system load are not locked.

| Metric | ON ms | OFF ms |
|---|---:|---:|
| GPU Total | 6.523433 | 5.779040 |
| Depth PrePass | 1.035723 | 0.000000 |
| GBufferPass | 2.073624 | 2.252262 |
| Wall Frame | 6.751157 | 5.998282 |
| CPU Record / submit | 1.428637 | 1.390133 |
| Depth + GBuffer | 3.109347 | 2.252262 |

GPU Total reduction: 11.41%.
on GPU Total run-average range: 6.227473-7.354052 ms.
off GPU Total run-average range: 5.362449-6.531925 ms.

## Implementation and correctness

Default remains ON. The executable accepts -BenchmarkDepthPrepass on/off and the runner -DepthPrepass on/off. OFF is restricted to benchmark automation, omits the depth-only geometry pass, enables GBuffer depth writes with the existing LESS_EQUAL comparison and declares the GBuffer pass as a depth writer in RenderGraph. Forward/ordinary rendering is not changed. Benchmark reports record depthPrepass.

Debug validation at 1000 instances, Hybrid, captures at frame 30 before measurement: ON/OFF PNG SHA256 both 4C8F5CB00D9644704E61E76C2EF96406A253B284A46D77675154B55EA3D70E12. Both exit normally, D3D12 ERROR 0. Release and Debug builds pass; existing CPU timing and EvaluationState tests pass. All ten Release reports passed dimensions, frame/sample counts, range and feature-setting checks.

This scene benefits from OFF: avoiding duplicated geometry submission outweighs the GBuffer visibility advantage at this view. This does not justify disabling depth prepass universally; scene overlap, draw order, material cost and instance count can change the balance. Raw files remain ignored under bin/Benchmark/depth-prepass-ab and bin/Benchmark/depth-prepass-validation. The runner now defaults to five repetitions and emits averages.csv in addition to per-run summary.csv.
