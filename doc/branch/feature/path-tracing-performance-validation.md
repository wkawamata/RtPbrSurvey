# Path tracing performance validation (Step 7)

Date: 2026-10-09. Branch: codex/pt-correctness-completion.
Step 6 was committed and pushed as 0105ae1.

## Status

- [x] Select the current executable explicitly instead of using a stale bin build.
- [x] Keep executable and renderer-source hashes in the measurement report.
- [x] Reject another RtPbrSurvey process before each launch, during telemetry and after each run.
- [x] Reject incomplete cohorts and unstable repeats before accepting scaling ratios.
- [x] Python regression tests: 114/114 passed.
- [x] Complete an isolated eleven-case, three-repeat measurement campaign.
- [ ] Accept a stable baseline and characterize scaling/per-sample cost.
- [ ] Identify measured bottlenecks and decide whether optimization is necessary.

Step 7 is not complete. The initial pilot was interrupted after detecting the
Work-2 application, PID 19232, at C:/work/rtpbrsurvey-work-2/bin/x64/debug/rtpbrsurvey.exe.
Only this chat's performance runner and its build/Debug test application were
stopped. Work-2 was not stopped or edited.

Six pilot runs retained 384 timing observations. They are excluded from accepted
performance evidence: concurrent GPU activity was not isolated, repeats were
incomplete, and timing tails were unstable. The 960x540 pilot had median 0.137472
ms but p95 10.510021 ms; this is not a valid scaling result. Do not merge these
observations into a later isolated cohort.

The new isolation preflight was tested with Work-2 still running. It returned a
failure and wrote status blocked before launching any measurement application.
Reports remain under ignored bin/PathTracingValidation, not committed raw output.

After explicit user approval, Work-2 PID 22564 was terminated and an isolated
repeat was started under completion-step7-isolated-20261009. A new Work-2 process,
PID 33168, appeared during that campaign. The isolation check rejected the run;
the runner cleaned up its own test application. This second campaign is also
excluded and does not establish accepted performance evidence. Work-2 launches
must remain paused throughout the performance campaign; repeated process killing
is not used to interfere with another workspace's tests.

## Completed retry: inconclusive

The completion-step7-isolated-20261009-r2 campaign completed all 33 runs with
2,112 measured timing observations. No competing RtPbrSurvey process was detected
by the preflight, sampled telemetry or post-run checks. The generated logs contain
no [ERROR] entries. This does not exclude GPU activity from other applications.

Executable: build/Debug/RtPbrSurvey.exe. SHA-256:
4c4acfd7aafc4dc7e78a0c043357386e76bc8e1d87851f7f0f2e5e09ad9bb26a.
GPU: NVIDIA GeForce RTX 2080 Ti, driver 616.56.
Raw report and assessment remain in the ignored campaign directory.

| Case | Median of run medians (ms) | Stability gate |
| --- | ---: | --- |
| Baseline | 2.943232 | Failed |
| 960x540 | 2.107040 | Passed |
| 1280x720 | 3.673456 | Passed |
| 2 samples/frame | 3.032704 | Failed |
| 4 samples/frame | 3.399856 | Failed |
| 1 segment | 2.953680 | Failed |
| 4 segments | 2.963680 | Failed |
| 4 lights | 2.977440 | Failed |
| 8 lights | 3.001232 | Failed |
| Low tessellation | 2.960976 | Passed |
| High tessellation | 2.940960 | Passed |

These are diagnostic observations, not accepted performance claims. Baseline run
medians range from 0.452608 to 2.965808 ms, with 85.39% spread and maximum
p95/median 6.47. Every acceptedTimeRatioToBaseline is null because the baseline
failed the predeclared gates. Do not select only fast repeats or relax the gates.

Measured telemetry includes changing power states and clocks. For example,
lights-1-r0 samples at logged frames 76 and 129 show P5/660 MHz and P8/585 MHz,
respectively. Sparse telemetry does not establish causality for individual timings.
Next work is to stabilize and diagnose the measurement environment before deriving
bottlenecks or optimizing the renderer. Step 7 remains incomplete.

## Foreground diagnosis: 2026-10-10

Helper subprocesses now use CREATE_NO_WINDOW so PowerShell and nvidia-smi do
not create visible console windows. Telemetry records the foreground process ID.
No renderer, GPU power policy or clock settings were changed.

The baseline-only completion-step7-hidden-monitor-20261010 cohort completed
three runs and 192 observations with the same executable. Per-run medians were
0.474544, 0.462640 and 0.660944 ms. The assessor still reports inconclusive:
41.79% median spread and maximum p95/median 1.7144. No D3D12 error/corruption
entries or competing RtPbrSurvey processes were detected.

The first run's measured telemetry sampled the test app as foreground at
P0/1350 MHz SM/7000 MHz memory. In the second run, both measured samples showed
another foreground PID, with P0/1500 MHz followed by P3/660 MHz and 5000 MHz
memory. The third run sampled another foreground PID and P3/765 MHz/5000 MHz.
This establishes that foreground state was not held constant; it does not prove
that focus or helper consoles caused every timing difference. The hidden-helper
cohort must not be pooled with the previous full matrix.

Next: run the baseline while the user leaves the measurement window foreground
and other GPU workloads idle. Require stable observed foreground and power/clock
telemetry before extending the campaign. Do not continuously steal focus from
the user or silently change system/NVIDIA power settings.

## Protocol

Use the current CMake Debug executable, fixed seed 7, constant-environment MIS,
and the existing roughness fixture. Keep VSync and window visibility policy
unchanged for all cases. Discard 64 completed GPU timing observations, then take
64 observations. Run three independently launched repeats with deterministic
randomized case ordering. Screenshot/readback observations are excluded.

Change one axis at a time from 1920x1080, 1 sample/frame, 2 segments, 1 light,
and sphere stacks/slices 24/32:

| Axis | Alternatives |
| --- | --- |
| Resolution | 960x540, 1280x720 |
| Samples/frame | 2, 4 |
| Segment limit | 1, 4 |
| Co-located light count | 4, 8; total intensity remains 8 |
| Sphere tessellation | stacks/slices 12/16, 96/128 |

The eleven-case matrix includes the baseline. Report pass GPU milliseconds,
per-run median and p95, and median-of-run-medians. Primary samples/second use
width * height * batch / pass duration; these are not actual traced ray counts.
This excludes scene rebuild, history reset, screenshot, and other rendering passes.

Predeclared diagnostic stability gates:

- At least three repeats, 64 discarded and 64 measured observations per repeat.
- Complete unique case/repeat matrix; finite positive GPU durations only.
- Run-median spread (max-min)/median <= 10% for each case.
- Every run's p95/median <= 1.25.
- Accept case/baseline time ratios only when both cases pass these gates.

These gates are not a statistical confidence interval. Inconclusive measurements
must be repeated or reported as inconclusive, not optimized against as facts.
Debug results and unlocked GPU clocks do not establish Release performance or
cross-GPU rankings. Process checks cover RtPbrSurvey, not all possible GPU clients;
keep other GPU workloads quiet and retain power/clock telemetry.

## Reproduction

Close other RtPbrSurvey applications before starting; the runner never closes them.
Use a new output directory for each campaign.

```powershell
python -B Tests/PathTracing/measure_performance.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step7-isolated-repeat --warmup 64 --frames 64 --repeats 3
python -B Tests/PathTracing/assess_performance.py bin/PathTracingValidation/step7-isolated-repeat/report.json --output bin/PathTracingValidation/step7-isolated-repeat/assessment.json
```

The original summarizer remains for historical reports. Step 7 uses the new
assessment without overwriting the old Part 4 evidence or requiring plotting packages.
