# Release RR performance measurement

## Prepared on 2026-11-04

Normal VS2022 Release x64 build succeeded (0 errors, one existing vcpkg duplicate-import warning).
The executable includes live PT RR and optional progress logging under -LogFPS.
Parser unit tests passed 3/3. CLI --help was checked. No GPU measurement was run:
the user requested keeping the other workspace renderer running and preparing only.
There are no new Release performance results in this document.

## Measurement protocol

Tests/PathTracing/measure_rr_release.py uses the static rr-quality scene, 4 bounces,
Pixel Center ON, Accumulate OFF, VSync OFF and D3D12 Debug Layer OFF.
It schedules Native/RR at 1280x720 and 1920x1080, 1/4 spp, three repeats each
(24 runs), shuffling case order per repeat. Each run drops 60 warmup observations,
measures 240 observations and excludes screenshot/readback work.
Other RtPbrSurvey processes are checked before and after each run; the script refuses
to measure while another one is running. It never terminates another workspace's app.

GPU total and active-pass timing summaries are recorded separately. A skipped RR pass
is not assigned an active SDK duration of zero. Latest-completed GPU observations may
repeat across CPU frames; they are not claimed to be unique GPU submissions.
New RR outputs/sec uses differences of verified completion counters over monotonic
wall time during the measured interval. Display-loop frequency is recorded separately.
A reset within that interval, wrong backend/resolution, insufficient RR outputs or
captured error invalidates the run. SDK pass timing includes its existing validation
work; it is not an isolated SDK-internal benchmark. Clock/power locking and exhaustive
monitoring of all other GPU clients are outside this initial harness.

## Run when GPU isolation is available

From the Work4 repository root after closing other renderer instances:

```powershell
& "C:\Users\wkawa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" -B Tests/PathTracing/measure_rr_release.py --output build/reconstruction-input-validation/performance/rr-release-run
```

Use a new empty output path for each run. Captures, logs and report.json stay local
under build/ and are not committed. Commands, executable hash and commit are included
in the report. Save the resulting interpretation separately after measurement.
