# NRD geometry bounds CPU profile

2026-11-17. RTX 3090, 1920x1080, static 121 DamagedHelmets, Hybrid benchmark, DEFAULT geometry, instancing OFF, normal GBuffer PS, Release, VSync/debug/UI off. Three independent processes, 120 warmup + 120 measured frames per process. No optimization or reconstruction policy changes in this experiment. CPU timer now has an exclusive NRD geometry bounds phase around NrdRelaxGeometrySnapshot::RecordTlas. It is subtracted from the parent Record / submit phase, not counted twice. Initial upload outside an active frame is not included.

| Metric | Median of run averages (ms) |
|---|---:|
| CPU Frame | 135.076622 |
| CPU NRD geometry bounds | 133.772889 |
| CPU Record / submit | 0.870714 |
| CPU Update / housekeeping | 0.294006 |
| CPU Present | 0.332734 |
| CPU GPU fence wait | 0.000000 |
| GPU Total | 35.994487 |

| Run | CPU Frame | NRD bounds | Share of CPU Frame | Remaining Record / submit |
|---:|---:|---:|---:|---:|
| 1 | 135.076622 | 133.772889 | 99.03% | 0.870714 |
| 2 | 134.505671 | 133.222343 | 99.05% | 0.860248 |
| 3 | 140.097609 | 138.694499 | 99.00% | 0.918730 |

## Conclusion

NRD bounds reconstruction dominates CPU elapsed frame time in all three runs. RecordTlas copies the instance list and calls BuildNrdRelaxWorldBounds, which transforms/checks each referenced index vertex for every visible instance on each TLAS rebuild. This is a measured dominant CPU cost, not just an O(N) hypothesis. Different runs have different GPU clocks/load; GPU totals here should not be interpreted as a regression caused by CPU instrumentation. CPU phase totals start slightly earlier than legacy CPU Frame timing, so exact equality is not required.

Next: identify all consumers of the snapshot token and world bounds, then implement demand-driven or cached bounds with explicit invalidation for geometry, transforms, instance count and reconstruction-mode changes. Do not merely skip RecordTlas while leaving a stale valid token; preserve NRD conservative-range safety and switching into NRD without reloading the scene. Benchmark Hybrid before/after and run NRD range/snapshot regressions separately. No shader optimization should precede resolving this CPU issue.

Validation: Release build and deterministic RendererCpuTiming tests pass, including explicit NRD phase accounting and retention across Begin. All three automation runs exited normally and passed per-metric sample/range checks and Hybrid settings checks. Renderer GPU work is unchanged. Raw reports and environment metadata remain ignored under bin/Benchmark/nrd-bounds-profile/.
