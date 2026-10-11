# Path tracing performance validation (Step 7)

Date: 2026-10-09. Branch: codex/pt-correctness-completion.
Step 6 was committed and pushed as 0105ae1.

## Status

- [x] Select the current executable explicitly instead of using a stale bin build.
- [x] Keep executable and renderer-source hashes in the measurement report.
- [x] Reject another RtPbrSurvey process before each launch, during telemetry and after each run.
- [x] Reject incomplete cohorts and unstable repeats before accepting scaling ratios.
- [x] Python regression tests: 115/115 passed.
- [x] Complete an isolated eleven-case, three-repeat measurement campaign.
- [x] Accept a stable baseline and characterize scaling/per-sample cost.
- [x] Record measured cost sensitivities and the profiling/optimization decision.

Step 7's eleven-case Debug timing characterization is complete for the measured
fixture and operating conditions. This is not a shader bottleneck attribution,
Release performance claim or general scene performance guarantee. See the accepted
full campaign below. Earlier incomplete/inconclusive campaigns remain excluded.

The initial pilot was interrupted after detecting the
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

## Follow-up baseline after commit 2a751e9

The completion-step7-foreground-20261010 cohort completed three runs and 192
observations. Medians were 0.477008, 0.469936 and 0.475440 ms. Median spread
improved to 1.49%, but maximum p95/median was 1.6583, so the result remains
inconclusive under the unchanged gates. No scaling ratio is accepted.

Measured telemetry sampled another foreground PID in run 0, but the test app
itself in runs 1 and 2. Run 2 still sampled P3/585 MHz SM/5000 MHz memory.
Foreground ownership alone therefore does not guarantee stable clocks or timing.
Do not proceed to a full matrix yet. The next diagnostic needs controlled GPU
power behavior or a sustained workload, preserving settings and reporting any
policy change explicitly; system power policy has not been changed here.

## Sustained workload diagnosis after commit 2eec7b6

The completion-step7-sustained-20261010 cohort used 1,024 discarded and 512
measured observations per run, with three independently launched baseline runs.
The executable, rendering settings and system/GPU power policy were unchanged.
All three runs completed: 1,536 measured observations; no D3D12 error/corruption
entries or competing RtPbrSurvey processes were detected.

| Repeat | Median (ms) | p95 (ms) |
| --- | ---: | ---: |
| 0 | 3.451360 | 5.107683 |
| 1 | 3.442800 | 5.098778 |
| 2 | 3.436512 | 5.099242 |

Median spread was 0.43%, but maximum p95/median was 1.4838, so the unchanged
stability gate still reports inconclusive. No normalized performance ratio is
accepted. Do not pool these observations with the short-warmup cohorts.

Each run retained ten measured telemetry samples. Every sample showed its own
test application as foreground, while GPU power state remained P5 with SM clocks
480-555 MHz and memory at 810 MHz. Temperatures were 32-33 C. Thus longer warmup
and foreground ownership alone did not establish the earlier P0/high-clock
operating condition. This is not proof of a renderer bottleneck or exact cause
of timing tails. The next controlled experiment should explicitly compare GPU
power-management conditions, with user-approved reversible settings and recorded
before/after policy. No such settings have been modified by this test.

## VSync diagnostic after commit 88a8945

The engine previously used Present(1, 0) unconditionally. An opt-in
-DisableVSync CLI flag now selects Present(0, 0); normal startup remains
Present(1, 0). This is not a tearing-mode or driver power-policy change.
The measurement runner supports --disable-vsync and records presentSyncInterval.

Debug x64 MSBuild succeeded with zero warnings/errors. Python regression tests
passed 115/115 after rerunning outside the temporary-folder sandbox restriction.
The same rebuilt executable was used for both cohorts, SHA-256:
d17659cd3500df855ec940278b9ccbdd17507fe3d594198f85157f174c0f9b69.

Each cohort used 256 discarded and 256 measured observations, three launches:

| Present interval | Run medians (ms) | Median spread | Maximum p95/median | Result |
| --- | --- | ---: | ---: | --- |
| 1 (VSync ON) | 3.398112 / 3.326512 / 3.345728 | 2.14% | 1.4818 | Inconclusive |
| 0 (VSync OFF) | 0.993360 / 3.314096 / 3.379824 | 72.01% | 3.5074 | Inconclusive |

All six runs completed, totaling 1,536 measured observations. No D3D12
error/corruption entries or competing RtPbrSurvey processes were detected.
Every measured foreground PID matched its test application. Both cohorts still
sampled P3/P5 with SM clocks around 435-540 MHz and memory at 810 or 5000 MHz.
Disabling application VSync alone did not establish stable high-clock operation.
The cohorts are separate evidence and do not establish an accepted speedup.
Their order was ON then OFF, not a randomized causal experiment.

Raw reports are under completion-step7-vsync-on-20261010 and
completion-step7-vsync-off-20261010 in ignored bin/PathTracingValidation.
Next: a user-approved reversible GPU power-management comparison or a GPU
profiler investigation; avoid repeating the full matrix before baseline stability.

## Next controlled comparison: per-program driver power policy

No driver policy was changed automatically. The available nvidia-smi inspection
does not provide the per-program control-panel setting used for this experiment.
On 2026-10-10, idle inspection showed P8 with Idle active and no active thermal
or software-power-cap clock-event reason. This idle snapshot does not diagnose
the measured workload or establish the current per-program power policy.

Manual preparation:

1. Open NVIDIA Control Panel, Manage 3D settings, Program Settings.
2. Select/add C:/work/RtPbrSurvey-work-3/build/Debug/RtPbrSurvey.exe.
3. Record the original Power management mode and whether it inherits the global
   setting. Do not alter Global Settings or other program settings.
4. Select Prefer maximum performance for this program and Apply.
5. Confirm the change to the measurement operator. Restart the test application
   for the next cohort; keep the executable and all rendering parameters fixed.
6. After the comparison, restore the exact original value/inheritance and Apply.

NVIDIA setting reference:
https://nvidia.custhelp.com/app/answers/detail/a_id/3130/

Measure the baseline with VSync ON, 256 discarded observations, 256 measured
observations and three launches. Use a new directory and the same executable
SHA-256 as the VSync cohorts. Record the user-confirmed original/new driver
policy in the result notes; do not claim it was independently queried by the
runner. Keep the window foreground. Assess using the unchanged gates and retain
all timing and clock telemetry. Only if the baseline becomes stable should the
eleven-case matrix be repeated under the explicitly recorded policy.

```powershell
python -B Tests/PathTracing/measure_performance.py --exe build/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/step7-prefer-max-baseline --cases baseline --warmup 256 --frames 256 --repeats 3
python -B Tests/PathTracing/assess_performance.py bin/PathTracingValidation/step7-prefer-max-baseline/report.json --output bin/PathTracingValidation/step7-prefer-max-baseline/assessment.json
```

Do not substitute global clock locking, power-limit changes or a system power
scheme change for this per-program comparison without separate authorization.

## User-confirmed Prefer maximum performance comparison

The user reported the original program setting as Use global setting (Optimal
power), then confirmed selecting Prefer maximum performance and clicking Apply
for build/Debug/RtPbrSurvey.exe. The runner did not independently query this
driver profile. Restore the original inherited setting after this comparison.

The completion-step7-prefer-max-20261010 cohort used the same executable hash
d17659cd3500df855ec940278b9ccbdd17507fe3d594198f85157f174c0f9b69,
VSync ON, 256 discarded observations, 256 measured observations and three launches.
All 768 measured observations were retained. No D3D12 error/corruption entries
or competing RtPbrSurvey processes were detected.

| Repeat | Median (ms) | p95 (ms) |
| --- | ---: | ---: |
| 0 | 0.456528 | 0.463320 |
| 1 | 0.454864 | 1.330112 |
| 2 | 0.463600 | 0.478464 |

Measured telemetry consistently sampled P0, 1350 MHz SM and 7000 MHz memory,
35-37 C. This contrasts with the earlier low-clock cohorts. Median spread was
1.91%, but maximum p95/median was 2.9242: assessment remains inconclusive.
The first run sampled its own application as foreground; runs 1 and 2 instead
sampled PID 8100, identified as Windows Explorer. Consequently foreground policy
was not constant across runs, and no accepted speedup/scaling ratio is claimed.
Do not discard the second run or mix cohorts to obtain a passing result.

The next baseline should retain the user-confirmed power policy while keeping
the application foreground for all runs. If the inherited policy is restored
first, explicitly reapply the per-program policy before that future experiment.
No driver/system setting has been changed automatically.

## Accepted baseline: Prefer maximum performance and foreground maintained

The completion-step7-prefer-max-foreground-20261010 cohort completed three
baseline runs with 256 discarded and 256 measured observations per run (768
measured observations). Same executable SHA-256 as the VSync comparison,
VSync ON and user-confirmed Prefer maximum performance policy retained.

| Repeat | Median (ms) | p95 (ms) |
| --- | ---: | ---: |
| 0 | 0.472528 | 0.481640 |
| 1 | 0.472112 | 0.482600 |
| 2 | 0.471040 | 0.479232 |

The assessment passed the unchanged gates: median of run medians 0.472112 ms,
median spread 0.3152%, maximum p95/median 1.0222. Every measured foreground
sample matched its test application's PID. All measured clock samples showed
P0, 1350 MHz SM and 7000 MHz memory, with temperatures 34-36 C. No competing
RtPbrSurvey processes or D3D12 error/corruption entries were detected.

This establishes an accepted Debug baseline for this fixture and operating
condition only, not completed scaling characterization or Release performance.
Step 7 remains incomplete until the full eleven-case, three-repeat cohort passes
under the same recorded operating conditions. Keep its baseline in that full
cohort; do not combine these runs with rejected or differently configured cohorts.
The next campaign should use 256 discarded and 256 measured observations per
run and preserve both power policy and foreground state throughout all 33 runs.

## Accepted full campaign

The completion-step7-full-prefer-max-20261010 campaign completed all eleven
cases with three launches per case, 256 discarded and 256 measured observations
per launch: 33 runs and 8,448 measured observations. All eleven cases passed the
unchanged 10% median-spread and 1.25 p95/median gates. No competing RtPbrSurvey
processes or D3D12 error/corruption entries were detected. Every measured
foreground sample matched its own application; all measured clock samples were
P0, 1350 MHz SM and 7000 MHz memory.

Operating policy: VSync ON; user-confirmed per-program Prefer maximum performance.
Same executable SHA-256 as the VSync and baseline cohorts:
d17659cd3500df855ec940278b9ccbdd17507fe3d594198f85157f174c0f9b69.
The original inherited setting is Use global setting (Optimal power). Restore it
after this experiment; future reproduction must explicitly record the policy.
The user confirmed restoring that original setting after the full campaign.

| Case | Median of run medians (ms) | Time / baseline |
| --- | ---: | ---: |
| Baseline (1920x1080, 1 sample, 2 segments, 1 light) | 0.460784 | 1.000 |
| 960x540 | 0.134176 | 0.291 |
| 1280x720 | 0.216240 | 0.469 |
| 2 samples/frame | 0.635568 | 1.379 |
| 4 samples/frame | 1.878016 | 4.076 |
| 1 segment | 0.428240 | 0.929 |
| 4 segments | 0.477184 | 1.036 |
| 4 co-located lights | 0.501760 | 1.089 |
| 8 co-located lights | 0.542688 | 1.178 |
| Low tessellation | 0.466944 | 1.013 |
| High tessellation | 0.501088 | 1.087 |

Baseline spread was 0.861%; its maximum p95/median was 1.0263. Across cases,
maximum median spread was 5.0203% and maximum p95/median was 1.0570.
Resolution scaling follows pixel count approximately, with fixed overhead more
visible at 960x540. Increasing the segment limit has a small cost in this fixture:
it does not demonstrate the cost of paths that actually traverse four surfaces.
Co-located light count increases pass time here while preserving total intensity;
this does not generalize to arbitrary shadow geometry or a light-selection scheme.

The clearest profiling candidate is sample batching: 2 samples cost 1.379 times
the baseline, whereas 4 cost 4.076 times. Four-sample raw values span roughly
1.22-2.14 ms with reproducible medians. Do not infer register pressure, occupancy,
bandwidth or a shader defect from these timestamps alone. No performance target
was specified; no shader optimization is justified solely by this fixture. Preserve
the correctness baseline, then use a GPU profiler if batch-4 performance matters.

Raw values/telemetry and machine-readable assessment remain in the ignored
campaign directory as report.json and assessment.json. This table preserves the
accepted summary without committing build products or logs. Previous cohorts
were not pooled or filtered to produce this acceptance.

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
