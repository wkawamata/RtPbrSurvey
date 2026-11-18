# NRD geometry bounds: demand gate and cache

2026-11-18. Bounds reconstruction is enabled only when PathTracing's effective reconstruction plan has runNrd=true (including NRD diagnostics/preview work). Hybrid/Native/RR without NRD work do not reconstruct bounds. TLAS still invalidates snapshot tokens, so disabling NRD cannot leave a valid stale token. Returning to NRD records the current TLAS again.

RecordTlas retains token invalidation on every call. It caches immutable-upload geometry's bounds keyed by the exact ordered world matrices and mesh IDs of the supplied instance span. Same inputs reuse cached bounds; instance count/order, mesh ID or transform changes rebuild. CaptureUpload/Reset discard the cache. Material and previous-world changes do not affect bounds. Failed/incomplete bounds remain invalid, including on cache hits. GPU geometry is still based on the immutable CaptureUpload copy.

## Validation

- Release and Debug builds succeeded.
- NrdRelaxAdapter tests pass: unchanged inputs reuse computation but invalidate old tokens; previous transform/material-only edits reuse cache; transform, count, mesh-ID and geometry-upload changes recompute; invalid snapshots export no valid range.
- Debug normal viewer capture: exit 0, D3D12 ERROR 0, PNG exactly matches the previous same-condition frame-60 reference.
- NRD-enabled cache safety is covered by host adapter tests; a native NRD SDK dispatch/switching integration run was not performed in this step.

## Hybrid timing

RTX 3090, static 121 Helmets, 1920x1080, DEFAULT geometry, instancing OFF, full GBuffer, VSync/UI/debug off. Three processes, 120 warmup + 120 measured frames each; median of per-run averages:

| Metric | Before (ms) | After (ms) |
|---|---:|---:|
| CPU Frame | 135.076622 | 2.575032 |
| CPU NRD geometry bounds | 133.772889 | 0.000000 |
| CPU Record / submit, excluding bounds | 0.870714 | 0.654267 |
| GPU Total | 35.994487 | 2.704299 |

The CPU bottleneck is removed from non-NRD Hybrid rendering. GPU timing also changes substantially when long CPU gaps disappear; no shader algorithm changed, and clocks were not locked, so do not attribute that GPU delta to a shader optimization. Original scene settings and output are preserved.

Raw evidence remains ignored under bin/Benchmark/nrd-bounds-gated and bin/Benchmark/nrd-bounds-gated-validation. The before run is bin/Benchmark/nrd-bounds-profile.