# RRQ-06 Release scheduling performance and image parity

Status: completed on 2026-11-19. Chained remains opt-in; eager remains the default.

## Method

RTX 3090, driver 621.24, Ryzen Threadripper 3970X. CMake Release, native RR and
NRD SDK enabled, application D3D12 Debug Layer OFF, VSync OFF. The integrated base
is origin/main 14c3d47 (#129/#130/#131); measured source is ff8b8b3641c7012ec72cd90aca337713bd132a6a.
The same executable, shader/runtime and asset hashes were checked across the cohort.
Executable SHA256: `869d72b64211bbdb87bbc3da8030f0c22abaafe33500f3053e2749343390e2db`.

WarmGlassEdges, static camera, 1 spp / 16 bounces / seed 7. Both schedules use the
same v1 numeric envelope. At each resolution: ABBA then BAAB, with A=safe-serial
and B=chained (four runs per schedule). Each run warms up 300 display frames then
measures 3000 display intervals. Rates use logged wall time, rather than assuming
equal durations or counting repeated retained images as new RR outputs.

Per-frame diagnostic logging and hidden windows are part of this reproducible
cohort; these numbers are not a production UI benchmark. GPU clocks were not locked.
No other RtPbrSurvey process was present at the pre/post-run guards. Other workspace
applications were not terminated. No CPU-heavy build ran during the measurements.

This compares safe-serial against chained, not the original eager input profile.
Do not combine these rates with RRQ-01: that older cohort predates main's VSync
presentation fix and used a different input profile/scheduler comparison.

## Results

Each cell is the mean of four run-level means/rates; fresh-RR ranges show run min-max.
CPU time is the logged CPU frame measurement. Retained age is sampled at UI frames,
not a display scanout latency. Packet time spans production recording to verified
CPU publication. None is a measurement of blocking CPU admission wait.

| Resolution | Schedule | Fresh RR/s (range) | Display FPS | CPU frame ms | Packet ms | Retained age ms |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | safe-serial | 146.50 (144.18-147.88) | 585.99 | 1.184 | 6.534 | 4.021 |
| 640x360 | chained | 162.12 (160.32-162.84) | 324.23 | 2.511 | 5.826 | 3.359 |
| 1280x720 | safe-serial | 50.41 (50.26-50.63) | 201.63 | 4.402 | 19.505 | 12.364 |
| 1280x720 | chained | 52.04 (51.83-52.19) | 104.08 | 9.007 | 18.833 | 9.920 |

640x360: fresh RR +10.7%; display FPS -44.7%.

1280x720: fresh RR +3.2%; display FPS -48.4%.

## GPU work and latency tails

Active-pass timings deduplicate delayed GPU packets and exclude display ticks where
a pass did not execute. A missing sample is not zero work. Reported p95 columns are
means of per-run p95 values, not a pooled population percentile.

| Resolution | Schedule | PT GPU ms | RR SDK GPU ms | CPU p95 ms | Packet p95 ms | Retained age p95 ms |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | safe-serial | 3.187 | 1.315 | 2.782 | 6.991 | 6.559 |
| 640x360 | chained | 3.308 | 1.335 | 4.250 | 6.181 | 6.008 |
| 1280x720 | safe-serial | 13.015 | 4.003 | 12.867 | 20.354 | 19.717 |
| 1280x720 | chained | 13.282 | 4.072 | 17.322 | 19.502 | 19.128 |

Chained has no CPU input-admission timestamp: inputWaitMs/admittedWaitMs and the
serial outputWaitMs are N/A. Its finalGpuWaitMs is host elapsed time between
recording and publication, not a GPU timer or CPU blocking duration. The report
preserves means/p50/p95 for available fields, active GPU passes and completed packets.

All 16 processes exited 0. Every final snapshot selected verified RR, resetCount=1,
with zero input/output rejection, cancellation and SDK failure. This Release cohort
has the debug layer disabled; Debug error checking is separate.

## Controlled image comparison

WarmGlassEdges and the tracked rr-quality helmet scene were each compared static
and with deterministic camera translation. Both schedulers produced exactly 16
verified outputs / 16 native calls / one reset. Camera pose advances per production,
not per display tick. All eight converted inputs and the selected final RR output
were byte-identical in all four pairs: RMSE=0, relative RMSE=0 and max absolute
error=0. This tests equivalent inputs/history, not subjective quality at every frame.

## Integration regression

The final ordinary Debug recovery suite passed 31 checkpoints and seven byte-identical
retained-image rejection comparisons with no D3D12 ERROR/CORRUPTION. The NRD+RR
Debug and Release regressions each passed 22 checkpoints, four independent
camera/object motion checks and actual NRD execution (ten SDK dispatches). The
integrated both-SDK Debug build had zero warnings/errors, and its regression had
no D3D12 ERROR/CORRUPTION. The Python timing parser passed five CPU-only tests.

An initial integrated Debug run completed semantic checkpoints but failed the
error-layer gate: ToneMap bound a table with an uninitialized lazy-depth descriptor.
That run remains failed in build/rrq04-main-integrated-20261119. Initializing a null
depth SRV before lazy materialization fixed it; build/rrq04-main-fixed-20261119 passed.

## Interpretation and reproduction

Chained increases fresh verified output cadence and reduces packet latency in this
cohort. It also reduces display FPS and increases measured CPU time per display frame.
Fewer presentation-only ticks and more production work per tick change these measures.
This is a scheduling tradeoff, not a general shader speedup or proof of a better default.
See [RRQ-07 adoption decision](rrq07-adoption.md).

```powershell
python -B Tests/PathTracing/measure_rr_queue_baseline.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --output build/rrq06-performance-new --comparison safe --repeats 2 --warmup 300 --frames 3000
python -B Tests/PathTracing/compare_rr_schedules.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --output build/rrq06-images-new
```

Use new/empty folders and an otherwise idle GPU. Local raw evidence:

- build/rrq06-main-integrated-performance-20261119/report.json and per-run logs/PNG.
- build/rrq06-main-integrated-images-20261119/report.json and nine ptbuf signals per run.
- build/rrq04-main-fixed-20261119/report.json.
- build/rrq05-main-integrated-20261119/report.json (Release).
- build/rrq05-main-integrated-debug-20261119/report.json (both-SDK Debug).

Logs, images, generated scenes and binaries remain excluded from Git.
