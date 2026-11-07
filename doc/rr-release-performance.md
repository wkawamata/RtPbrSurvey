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

## Packet-latency instrumentation (2026-11-06)

Commit 3a63b4e adds optional [RR Packet] records alongside -LogFPS/-LogToFile.
No scheduling, history, validation, resource lifetime or fence-wait policy changes.
Only successfully verified packets publish latency records. The parser deduplicates
repeated display observations by completion count and limits records to completions
inside the measurement interval. Parser tests pass 5/5, including deduplication and
nonfinite rejection. Older logs without packet records remain readable.

Fields use steady-clock CPU observation times, NOT GPU timestamps:
- inputWaitMs: RecordInput entry to the Poll that admits its completed validation.
  Includes submission/queue/GPU work and CPU polling cadence, not validation alone.
- admittedWaitMs: admitted Poll to the native evaluate API call (including recording).
- evaluateCpuMs: time spent inside EvaluateStreamlineRayReconstruction on the CPU.
- outputWaitMs: output validation recorded to the Poll that publishes verified output.
- totalMs: RecordInput entry to verified publication. It excludes CPU work
  before RecordInput and time between that publication and the next packet. Prior
  queued PT GPU commands can still contribute to inputWaitMs.
- inputPolls/outputPolls: Poll calls while each pending phase was active, including
  the admitting/publishing call; these are observations, not unique GPU frames.

The component durations do not exactly sum to totalMs: setup/recording between
boundaries remains included only in totalMs. No GPU SDK-isolated duration is inferred.
There are no new usable latency measurements yet. The diagnostic cohort
rr-packet-latency-20261106 stopped on its first case because RR fell back to Native:
slIsFeatureSupported returned Result::eErrorFeatureNotSupported (Unsupported adapter).
The pre-instrumentation Debug binary returned the same result in
rr-support-probe-20261106.log, so this is not established as a new-code regression.
NVIDIA-SMI still reports RTX 2080 Ti and driver 616.56; cause remains unresolved.
Do not treat the fallback run as RR timing or overwrite the accepted baseline above.

Source review found a concrete next investigation target: the validation shader
issues a global InterlockedAdd to the same counter for every pixel, in both input
and output validation. A wave/group reduction could preserve per-pixel finite/status
checks and exact coverage while reducing global atomic contention. Its benefit is
not measured or implemented here. Validate rejection masks, edge dispatch groups and
full output coverage before accepting any such change. Separate SDK GPU timing from
poison/validation work before attributing the entire existing SDK pass cost to NGX.

Instrumentation validation: Debug and Release builds succeeded (0 errors, one
existing MSB4011 warning each). Debug telemetry smoke captured successfully with
D3D12 debug layer ON and no captured ERROR/CORRUPTION; RR remained unavailable,
so only fallback/logging behavior was checked, not the new live packet timings.
All 8 prior baseline logs retain exactly the same GPU total and completion delta
when parsed by the extended parser. New live latency values remain unverified.

## Validation reduction (2026-11-06)

Implemented a groupshared tree reduction for the existing 8x8 validation dispatch.
Each in-bounds pixel still checks the same input status or finite output RGB.
All 64 lanes, including edge padding, participate in group barriers. Padding lanes
contribute zero to both mask and count. Group lane zero writes the reduced error
mask (only when nonzero) and pixel count to the unchanged 8-byte summary.
This adds no wave-ops hardware requirement, resources, CPU waits or history changes.

At full groups the global coverage additions decrease from one per pixel to one
per 64 pixels: 921600 -> 14400 at 720p, 2073600 -> 32400 at 1080p, per validation.
Invalid category bits are OR-reduced without losing categories. Output alpha remains
ignored exactly as before. Input/output rejection and fence-gated publication remain.
These are operation-count reductions, not measured end-to-end speedups.

Tests/RrValidationTests.cpp compares the optimized shader and frozen scalar reference
against independently calculated CPU summaries, with D3D12 debug layer enabled.
Tests/shaders_RrValidationReference.hlsl matches the production shader at 56bd712
(normalizing CRLF only). CMake target/test: RtPbrSurvey.RrValidationTests / RrValidation.
Cases cover 1x1, 7x9, 8x8, 17x19, 65x3, 720p and 1080p; input and output modes;
valid, mixed/all-invalid, categories 1..6, fractional/negative/out-of-range values,
NaN and both infinities, and ignored nonfinite alpha. Large cases cover valid and
mixed-invalid payloads. Forty-eight fixtures, two GPU implementations each, passed
on WARP with exact mask/count equality and D3D12 errors 0. Test Release build had
0 warnings/errors; application Debug/Release builds passed with the existing MSB4011.

Hardware timing was NOT run: isolation check found another workspace's Debug app
(PID 106312) and stopped before launching the test. It was not terminated. There
are no new native RR throughput results or hardware timing claims. The executable
supports --hardware once isolation is available; it prints one-dispatch diagnostic
timestamps (debug layer ON, no clock locking/repetitions), not a complete benchmark.
After isolated hardware equivalence, rerun the Release cohort and camera/object
lifecycle checks with RR available before accepting a runtime performance gain.

## Hardware follow-up (2026-11-07)

See [hardware verification](rr-hardware-validation-20261107.md) for hardware
equivalence, a completed 24-run Release cohort and observed packet latencies.
The earlier unavailable/pending entries above describe historical attempts.
