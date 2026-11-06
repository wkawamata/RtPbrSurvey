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

## Measured on 2026-11-06

RTX 2080 Ti; normal VS2022 Release x64, static rr-quality scene, 4 bounces,
center primary, Accumulate OFF, VSync OFF, app D3D12 debug layer OFF.
Each accepted case uses three runs of 60 warmup plus 240 measurement observations.
All 24 accepted runs passed backend, dimensions, history and finite timing checks;
no captured ERROR/CORRUPTION. This is a performance cohort, not equal-quality output.

The initial attempt requested 1920x1080 but Windows supplied 1920x1061. It failed
validation and is excluded. WM_GETMINMAXINFO now permits a 4096x4096 client plus
window decorations, matching the CLI limit; normal maximize behavior is unchanged.
Debug and Release builds succeeded, each with one existing MSB4011 warning.
Parser unit tests passed 3/3.

The first repeat in rr-release-exact-20261106 overlapped a Debug build and is
excluded in full. Accepted evidence consists of repeats 1 and 2 there plus repeat 0
in rr-release-clean-repeat-20261106 (three runs per case). No build ran during
those accepted intervals. Logs/captures/report JSON remain local under build/.
Both cohorts use executable SHA256
29d6486eb868d3e88486e41b01bc94bdba2ae112c34b48a63949aeaf35cbd971.
The initial cohort records parent commit ddb2027 with the then-uncommitted window
fix; replacement cohort records committed 51d9b9a, with identical executable hash.
Accepted run manifest: rr-release-accepted-summary-20261106.json.

Values below are arithmetic means of the three per-run means. GPU timings are
latest-completed observations and may repeat, as described above. Conversion and
SDK columns average observations where that pass actually ran.

| Resolution | spp | Native GPU total (ms) | RR new outputs/s | RR conversion (ms) | RR SDK pass (ms) |
|---|---:|---:|---:|---:|---:|
| 1280x720 | 1 | 5.11 | 30.70 | 0.344 | 12.02 |
| 1280x720 | 4 | 16.73 | 22.75 | 0.365 | 12.40 |
| 1920x1080 | 1 | 11.29 | 13.59 | 0.930 | 28.55 |
| 1920x1080 | 4 | 37.07 | 10.12 | 0.773 | 27.23 |

RR fresh-output ranges across the three runs:
- 1280x720, 1 spp: 30.11..31.34 outputs/s.
- 1280x720, 4 spp: 22.51..23.06 outputs/s.
- 1920x1080, 1 spp: 12.90..14.04 outputs/s.
- 1920x1080, 4 spp: 9.75..10.64 outputs/s.

The measured conversion cost is appreciably smaller than the SDK pass cost. The
SDK pass includes existing clear/validation work; it is not SDK-internal timing.
The whole shared-input architecture's overhead has not been isolated by A/B tests.

RR display-loop observation rates were about 153/113 Hz at 720p (1/4 spp) and
68/50 Hz at 1080p, while fresh RR rates were only 31/23 and 14/10 outputs/s.
Do not present display-loop FPS or averaged per-display-loop RR GPU time as a
Native-to-RR speedup. The single-flight publication path reuses a completed image
between fresh outputs; packet scheduling and SDK pass cost merit investigation.
The measurements do not establish an equal-quality Native/RR speed comparison.

Next performance work should first instrument packet-stage latency and distinguish
SDK work from validation overhead, then assess safe scheduling improvements while
preserving immutable inputs and verified output publication. Avoid removing guards
or changing history correctness merely to improve display FPS. The accepted moving
shadow ghosting remains a separate known image-quality limitation.

Debug follow-up with D3D12 debug layer enabled confirmed exact 1920x1080,
verified RR output (8 completed, 7 continuous, reset 1, cancelled 0), successful
capture and no captured ERROR/CORRUPTION. Evidence: rr-window-debug-20261106.log
and PNG in the same local performance folder.
