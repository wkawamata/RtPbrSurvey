# RRQ-08 CPU wait decomposition

Status: diagnostic follow-up completed on 2026-11-19. No rendering, queue, history
or presentation policy changed. Chained remains opt-in; eager remains the default.

## What was added

The existing exclusive RendererCpuTimer intervals now accompany each logged
RenderGraphFrame: update/housekeeping, record/submit, GPU fence wait, Present and
NRD bounds, plus their total. This adds JSON diagnostics only when LogFPS runs;
no additional fence waits, GPU queries or CPU timer scopes are introduced.
The parser checks complete observations, stable phase keys, nonnegative/finite
values and agreement between the phase sum and total. Old logs without these
fields remain readable with unavailable phase statistics, rather than fake zeros.

Per-frame statistics and per-verified-output costs are separate. The latter sums
CPU phase wall time over display intervals after the left endpoint, divided by
the verified-completion delta in that same window. It is aggregate normalization,
not attribution of every CPU interval to a particular RR packet. Non-fence time
still includes driver/SDK behavior inside record/submit and is not measured CPU
busy cycles or thread utilization.

## Measurement

RTX 3090 / driver 621.24, CMake Release, Debug Layer OFF, VSync OFF, static
WarmGlassEdges, 1 spp / 16 bounces / seed 7. Safe-serial/chained use identical v1
inputs. One ABBA per resolution (two runs per schedule), 150 warmup display frames,
1500 measured display intervals. No competing RtPbrSurvey process was present at
the guards; no build ran during the cohort. All eight runs exited 0 with verified
RR, one reset, and zero rejects, cancellations or SDK failures.

Source: `968733eb3747b199b9882bfc73c507b61cc24421`.
Release SHA256: `585587470ad45f5caa423a136649703e6c857bee79b8e1c82618a918622d934a`.

Cells are means of two run-level means/rates. Extra per-frame JSON and shorter
windows distinguish this diagnostic cohort from RRQ-06; do not combine absolute
rates across those cohorts to infer a rendering regression.

| Resolution | Schedule | Fresh RR/s | Display FPS | Renderer CPU ms/frame | Fence wait ms/frame | Present ms/frame |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | safe-serial | 138.86 | 554.68 | 1.409 | 0.716 | 0.140 |
| 640x360 | chained | 153.26 | 306.53 | 2.838 | 2.002 | 0.138 |
| 1280x720 | safe-serial | 48.42 | 193.67 | 4.725 | 3.919 | 0.161 |
| 1280x720 | chained | 49.79 | 99.58 | 9.515 | 8.371 | 0.198 |

| Resolution | Schedule | Non-fence CPU ms/RR | Fence wait ms/RR | Record/submit ms/RR | Present ms/RR | Update ms/RR |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | safe-serial | 2.767 | 2.862 | 1.383 | 0.558 | 0.826 |
| 640x360 | chained | 1.671 | 4.007 | 0.963 | 0.276 | 0.432 |
| 1280x720 | safe-serial | 3.221 | 15.687 | 1.668 | 0.642 | 0.911 |
| 1280x720 | chained | 2.286 | 16.754 | 1.348 | 0.396 | 0.542 |

## Findings

The measured cadence is approximately one new RR output per four display intervals
for safe-serial, and one per two for chained. A display interval can reuse the same
verified image. Thus lower display FPS does not mean lower reconstructed-image
update rate. Chained reduces normalized non-fence CPU work by performing fewer
presentation/update passes per verified output, even though each display frame
contains more production work and wait time.

The final RR publication poll is nonblocking: PathTracingRrLive::Poll checks the
completed fence and returns immediately if the packet is still pending. It maps
and inspects the input/output summaries only after fence completion. There is no
WaitForFenceValue inside that publication check.

The dominant explicit blocking interval is GPU fence wait, not Present. Code
inspection locates the regular frame wait in MoveToNextFrame: after queue Signal
and Present, the CPU waits before reusing the next backbuffer's command allocator,
constant buffers and descriptor staging chunk. A chained GPU production is longer
than either of the two serial submissions; CPU backpressure therefore becomes
more concentrated. This does not prove that the GPU is idle during the wait.
The PT and RR GPU passes remain the main production cost; CPU input admission
has already been removed from the chained packet.

Increasing backbuffer count or adding cheap re-presentations could inflate display
FPS without increasing fresh RR/s, reducing input-to-output latency or proving
better quality. Such a change is not justified by the FPS counter alone.

## Next implementation boundary

A useful next experiment is nonblocking frame-resource acquisition, so the main
Win32 loop can return to PeekMessage while the required allocator fence is pending.
It must preserve these conditions:

- Split the last-submitted/retire fence from the next-to-signal value. The current
  FrameResource fence field changes roles around the blocking wait; simply deleting
  that wait would allow allocator/CB/descriptor reuse while still in flight.
- Check readiness before renderer update, descriptor staging, command allocator
  reset or GPU recording. A not-ready tick must not advance PT samples, RR production,
  history transforms, capture plans or presented-frame/FPS counters.
- Keep UI-frame finalization and settings/resize/backend requests consistent when a
  render is deferred. Do not dispatch arbitrary window messages recursively inside
  RenderFrame's GPU wait. Use the existing outer message loop and a bounded/event
  wait to avoid a busy loop.
- Keep final summary validation and immutable RR publication. Retest pending pause,
  resize, scene/backend changes and rejected output before accepting the experiment.
- Measure input-service latency separately. Returning to the message loop cannot
  make the same GPU queue draw UI while a long PT/RR production occupies that queue;
  this change alone does not promise higher fresh RR/s or visual smoothness.

This follow-up implements the diagnostics and establishes the baseline above. The
nonblocking acquisition design is the next separate experiment, not an enabled mode.

## Verification and reproduction

Seven CPU parser tests passed, including legacy logs, phase normalization, broken
phase totals and negative timing rejection. Ordinary Debug and CMake Release built
successfully (one existing MSB4011 warning in the ordinary project). A 60-frame Debug
chained RR capture passed: 41 selected observations had all five phases, the output
was verified RR, and no D3D12 ERROR/CORRUPTION was captured.

```powershell
python -B Tests/PathTracing/test_rr_queue_baseline.py
python -B Tests/PathTracing/measure_rr_queue_baseline.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --output build/rrq08-cpu-new --comparison safe --repeats 1 --warmup 150 --frames 1500
```

Use a new/empty folder and an otherwise idle GPU. Local evidence:

- build/rrq08-cpu-phases-release-20261119/report.json and per-run logs/PNG.
- build/rrq08-debug-phases-20261119/report.json and d3d12.log.
- build/agent-builds/rrq08-cpu-phases-debug-20261119 and rrq08-cpu-phases-release-20261119.

Generated reports/captures remain local and excluded from Git.
