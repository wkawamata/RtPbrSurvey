# RRQ-01 queue baseline results

Status: complete. Measurement only; no rendering policy changes in RRQ-01.

## Reproduction and scope

Date: 2026-11-17. RTX 3090, NVIDIA driver 621.24, Windows WDDM.
Release executable built on 2026-11-16 from rendering baseline 1b3120e;
measurement script/source commit: 1e7cba99e73d969574ecea41f6e9555db0b4b023.
Executable SHA256: e24ddc07967f1e769d032845424587fc07b054497b28a866f81f82434c5ad093.

The earlier Work5 application had exited before measurement. No other
RtPbrSurvey process was found before/after each run; no process was stopped.
Desktop GPU clients remained open. These are controlled local observations,
not an exclusive GPU reservation or a guarantee of zero background activity.

WarmGlassEdges, fixed camera, seed 7, 1 spp / 16 bounces, continuous RR,
native SDK evaluation enabled, native resolution, VSync off, app debug
layer off. Strict/eager/eager/strict ordering at each resolution. Both modes
use the same executable and validation gates; strict retains the Ready tick.

The initial 60-warmup/240-interval cohort passed all eight runs, but its
measurement intervals lasted only about 1-2 seconds. The primary cohort below
extends this to 300 warmup frames and 2,400 intervals (about 8-15 seconds/run).
All 16 runs verified native RR output, with zero input rejections/cancellations
and no logged ERROR/CORRUPTION. Hashes of executable, runtime/shaders and
assets remained unchanged across each cohort. CPU-only parser tests: 4 passed.

```powershell
python -B Tests/PathTracing/measure_rr_queue_baseline.py --exe bin/x64/Release/RtPbrSurvey.exe --output build/rrq01-baseline-long-new --warmup 300 --frames 2400
```

## Primary observations

Each row is one process run. Display rate uses wall time between logged frames;
fresh RR rate counts only newly verified publications over that same interval.

| Resolution | Mode/run | Seconds | New RR outputs | Display frames/s | Fresh RR outputs/s | CPU frame mean/p95 ms |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | strict/0 | 8.139 | 480 | 294.88 | 58.98 | 2.796/3.490 |
| 640x360 | eager/1 | 7.921 | 600 | 303.00 | 75.75 | 2.707/3.210 |
| 640x360 | eager/2 | 7.881 | 600 | 304.55 | 76.14 | 2.694/3.170 |
| 640x360 | strict/3 | 8.061 | 480 | 297.72 | 59.54 | 2.772/3.410 |
| 1280x720 | strict/0 | 13.696 | 480 | 175.24 | 35.05 | 5.007/13.310 |
| 1280x720 | eager/1 | 15.218 | 600 | 157.71 | 39.43 | 5.623/13.240 |
| 1280x720 | eager/2 | 14.774 | 600 | 162.44 | 40.61 | 5.508/13.270 |
| 1280x720 | strict/3 | 13.290 | 480 | 180.58 | 36.12 | 4.901/12.870 |

Duration-weighted rates across the two repetitions per mode:

| Resolution | Strict fresh RR/s | Eager fresh RR/s | Change | Strict display/s | Eager display/s |
|---|---:|---:|---:|---:|---:|
| 640x360 | 59.26 | 75.94 | +28.2% | 296.29 | 303.77 |
| 1280x720 | 35.57 | 40.01 | +12.5% | 177.87 | 160.04 |

The strict/eager cadence is exactly 480/600 publications per 2,400 intervals
in these runs: one new output per five/four display frames. At 720p eager
produces more new RR images per second while display rate falls. It schedules
more expensive PT/RR work per display interval, so presentation rate and fresh
content rate must remain separate acceptance metrics.

## GPU and packet timing

Count-weighted means across two repetitions, in milliseconds. GPU measurements
are deduplicated by completed GPU packet ID. Active-pass means exclude frames
where that pass did not execute.

| Resolution/mode | GPU total per observed packet | PT active | Conversion active | Admission active | RR native active |
|---|---:|---:|---:|---:|---:|
| 640x360 strict | 1.177 | 3.879 | 0.059 | 0.010 | 1.508 |
| 640x360 eager | 1.230 | 3.257 | 0.057 | 0.009 | 1.297 |
| 1280x720 strict | 3.601 | 13.329 | 0.212 | 0.019 | 3.972 |
| 1280x720 eager | 4.496 | 13.369 | 0.211 | 0.019 | 4.010 |

| Resolution/mode | Input wait | Admitted to SDK call | SDK CPU call | Output wait | Packet total |
|---|---:|---:|---:|---:|---:|
| 640x360 strict | 4.910 | 2.222 | 0.348 | 3.970 | 11.470 |
| 640x360 eager | 4.290 | 2.206 | 0.342 | 3.935 | 10.792 |
| 1280x720 strict | 14.740 | 2.321 | 0.376 | 5.031 | 22.492 |
| 1280x720 eager | 14.777 | 2.325 | 0.377 | 5.065 | 22.569 |

Packet statistics include the retained packet observed at the left interval
boundary, once per packet; publication-rate deltas use the exact endpoints.
Input/output waits are host wall time including GPU execution and polling
cadence, not pure CPU blocking time. Admitted-to-call time includes subsequent
CPU frame preparation. SDK CPU call measures EvaluateStreamlineRayReconstruction,
not the GPU kernel. PT DLSS RR Native includes output validation.
GPU readback is delayed relative to the CPU observation interval.

## Interpretation and next boundary

Immediate next-production scheduling removes one presentation-only tick.
It does not eliminate the input admission round trip or the output verification
round trip. The roughly two-millisecond admitted-to-call interval and separate
input/output polling stages provide concrete targets for the next design.
These timings must not be added up as proof of a guaranteed speedup.

RRQ-02 should define SDK-safe numeric inputs, separate input/output validation
summaries, immutable resource lifetimes, rejected-packet publication rules and
SDK history recovery before trying a same-queue PT/conversion/RR chain.
No such pipeline change has been implemented here.

The automated window was hidden and frame logging was enabled. The results
are a reproducible baseline for this fixture, not interactive UI performance,
general scene performance, or subjective quality acceptance. Two repetitions
per mode and short local runs do not establish confidence bounds. PNGs were
captured for later review; no new image-quality claim is made.

## Local evidence

- build/rrq01-baseline-20261117/report.json: initial eight-run cohort.
- build/rrq01-baseline-long-20261117/report.json: primary eight-run cohort.
- Both folders contain per-run logs and PNGs; they are excluded from Git.
- Reports preserve settings, commands, hashes, diagnostics and distributions.
- build/agent-builds/rrq01-release-20261116/build.exitcode: successful Release build.
