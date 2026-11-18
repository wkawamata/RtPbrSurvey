# Draw order / Depth PrePass / PS invocation comparison

2026-11-18. Same Release binary, RTX 3090, 1920x1080 Hybrid, static 1000 DamagedHelmets, DEFAULT geometry, instancing OFF, normal GBuffer PS, VSync/debug/UI off. Four conditions, five independent runs each; each run has 120 warmup + 120 measurement frames. Cases run in forward and reverse case order on alternating repetitions. Values are arithmetic means of five per-run averages, not medians. GPU clocks/system load are not locked. All scene configurations, adapters and executable hashes match.

## Results

| Draw order / PrePass | GPU Total ms | Depth ms | GBuffer ms | Depth + GBuffer ms | PS invocations/frame | GPU run range ms |
|---|---:|---:|---:|---:|---:|---:|
| forward-on | 6.623386 | 1.040536 | 2.108860 | 3.149396 | 2015144 | 6.270839-6.988894 |
| forward-off | 5.672796 | 0.000000 | 2.182366 | 2.182366 | 2281152 | 5.382519-5.976627 |
| reverse-on | 6.634615 | 1.039239 | 2.111843 | 3.151082 | 2015144 | 6.204561-6.921549 |
| reverse-off | 7.769953 | 0.000000 | 4.395317 | 4.395317 | 8522259 | 7.617562-7.879663 |

## Interpretation

The default Grid order is ascending Z, near-to-far for the fixed default camera. Reverse flips geometry draws for both Depth and GBuffer without changing instance IDs, materials, object IDs, transforms or TLAS order. Instancing is disabled for this controlled test. PrePass OFF enables GBuffer depth writes.

Near-to-far already rejects most hidden PS work without a prepass: PrePass reduces GBuffer PS invocations by about 11.7%, but costs another geometry pass. Far-to-near has much higher overdraw without a prepass: PS invocations rise to about 8.52 million; PrePass reduces this by about 76.4% to the same 2.02 million as near-to-far. Here PrePass pays for itself. This experimentally confirms draw order as a cause of the earlier result, not absence of geometric overlap. It does not prove the historical renderer used a different order or isolate changes due to the old GPU.

## Instrumentation and correctness

D3D12 PIPELINE_STATISTICS queries wrap GBufferPass. Query data is resolved into frame-local READBACK buffers, then mapped only after the existing reused-frame fence wait. PS invocations count shader invocations, not unique visible pixels; helper execution may contribute. Completed-frame delay semantics match GPU timestamps. Query overhead is included in every condition, so compare within this same-binary test.

Debug validation at 1000 instances: all four cases exit 0 with D3D12 ERROR 0, valid eight-frame statistics and one identical PNG hash across all four images. Release/Debug builds and existing RendererCpuTiming/EvaluationState tests pass. All 20 Release reports pass sample/range/resolution/order/setting validation. Normal rendering defaults remain unchanged; reverse order is benchmark-only.

Raw data: bin/Benchmark/draw-order-depth-ab/ and bin/Benchmark/draw-order-depth-validation/. Aggregate: comparison.csv. Executable SHA256: 457A16B53E398F74C44A5507FF98373056746221B64389832BF09370E4D10168.
