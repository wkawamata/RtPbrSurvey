# RRQ-01 baseline preparation

Status: prepared; GPU measurement pending external GPU availability.

Rendering source baseline: 1b3120e (eager publication), with strict comparison
selected using -PathTracingRrStrictSchedule. No rendering policy changes are made
by this task. Release build prepared successfully on 2026-11-16 (one existing
MSB4011 warning). On resumption, Work5 RtPbrSurvey Release PID 38336 was active
and GPU utilization was 22 percent. No application was stopped and no measurement
was launched. Process IDs/load are point-in-time observations, not a standing lock.

## Frozen cohort

- WarmGlassEdges, fixed saved camera and seed 7, 1 spp / 16 bounces.
- 640x360 and 1280x720; each uses strict/eager/eager/strict ordering.
- VSync off, Release with debug layer off; 60 warmup frames and 240 intervals.
- Capture one PNG per run for subsequent image comparison. These do not by
  themselves establish subjective quality or a new image-error threshold.
- Require absence of other RtPbrSurvey processes before and after each run.
  Operator must additionally confirm other GPU tasks are idle: the process-name
  guard alone cannot detect every possible GPU workload.
- Pin executable, shader/SDK runtime, script and scene/shared-asset SHA256s.
  Reject fallback, cancelled/rejected packets, settings drift, invalid timings,
  incomplete measurement intervals or changed runtime/assets.

## Measures and interpretation

Report display interval rate separately from new verified RR publications per
second. CPU frame time is from the log (two decimal places). GPU totals/pass
samples are deduplicated by completed packet ID, not charged again at each UI tick.
GPU packet timing is delayed relative to the CPU observation interval.

Input wait, admitted wait, SDK evaluate CPU time, output wait and packet latency
are separately reported with count/mean/p50/p95. Host wall waits include GPU work
and polling cadence; they are not pure CPU blocking time. The PT DLSS RR Native
GPU pass includes output validation and is not an isolated SDK-kernel timer.
Statistics are observed distributions; two repetitions per mode are not broad
confidence bounds. Performance thresholds and RRQ-02 safety contracts remain unset.

## Execution when the GPU is available

From repository root:

```powershell
python -B Tests/PathTracing/measure_rr_queue_baseline.py --exe bin/x64/Release/RtPbrSurvey.exe --output build/rrq01-baseline-new
```

Output must be a new/empty folder. Generated logs, PNGs and JSON remain local.
Before starting, recheck GPU users/load and executable provenance after any
intervening build. No background monitoring or automatic application shutdown
is scheduled by this preparation.

CPU-only parser verification passed four tests: rate/count separation, duplicate
packet deduplication, missing interval/counter regression rejection and nonfinite/
negative timing rejection. RRQ-01 is not complete until the isolated cohort and
its summary report have been produced.
