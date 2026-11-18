# RTX 3090 instancing ON/OFF

2026-11-17. Static vertex/index geometry now always uses DEFAULT heaps. The
legacy direct-UPLOAD branch, -UseUploadGeometry, engine toggle and runner
-GeometryHeap option were removed. Transfer staging remains alive through the
scene upload fence. The earlier heap comparison remains as historical evidence.

Instancing merges only contiguous instance indices with identical geometry
ranges. Invalid-transform gaps and geometry changes split batches. Instance
ordering and sceneInstanceOffset are preserved; the existing VS adds
SV_InstanceID to that offset for materials, transforms, motion vectors and IDs.
DrawIndexedInstanced and DrawInstanced both use the merged instance count.
No shader or ray-tracing instance mapping changes were made.

The executable accepts -BenchmarkInstancing on/off; the runner accepts
-Instancing on/off (default off). Reports record the setting and verify DEFAULT
geometry placement. Ordinary execution also defaults to instancing off.

## Measurement

RTX 3090, same Release binary, 1920x1080, static 121 Helmets, minimal five-pass
raster mode, VSync/debug/UI off. Each process: 600 warmup + 600 measured frames.
Three runs each, in OFF/ON, ON/OFF, OFF/ON order. Values are medians of per-run
means in milliseconds; clocks were not locked.

| Metric | OFF | ON |
|---|---:|---:|
| CPU Frame | 0.684395 | 0.688310 |
| Wall Frame | 0.965062 | 0.962171 |
| CPU Record / submit | 0.244269 | 0.215000 |
| GPU Total | 0.850309 | 0.840315 |
| GBufferPass | 0.576852 | 0.574560 |

CPU recording improves by about 12%, GPU Total by about 1.2%, while overall
wall time changes by only about 0.3%. Do not claim a material frame-rate gain
from this short comparison. Keep the explicit switch for future benchmarks.

## Validation

- Release and Debug builds succeeded.
- Debug raster mode, instancing ON, 121 instances, 4 warmup + 8 measured frames:
  normal exit, D3D12 ERROR 0.
- All six benchmark runs passed settings, frame-count, metric-range and exact
  five-pass checks.
- Existing RendererCpuTiming and EvaluationState tests pass.
- No pixel-image equivalence test was performed for instancing in this step;
  the earlier PNG equality result applies only to the heap-placement change.
- Raw output: bin/Benchmark/instancing-ab/{on,off}-{1,2,3}/.

Example:

```powershell
.\Tests\Invoke-HelmetBenchmark.ps1 -Instances 121 -Instancing on -WarmupFrames 600 -Frames 600 -Repeats 3
```
