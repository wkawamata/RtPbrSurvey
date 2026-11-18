# RRQ-09 nonblocking frame acquisition experiment

Status: prototype implemented; opt-in only. Default acquisition remains blocking.
Use `-NonblockingFrameAcquisition` at startup, or toggle
Rendering > DLSS RR > Nonblocking frame acquisition (experimental) in the UI.
UI changes occur after host acquisition and before recording, so switching OFF
cannot bypass a pending retire fence. The checkbox is process-local and not saved
in Evaluation Cases. No saved scene setting or
RR admission policy changes. This flag is a host/frame-resource experiment, not
an async GPU queue or GPU-only RR publication mode.

## Ownership and host behavior

After submission/Present, MoveToNextFrame still signals the submitted frame and
selects the next backbuffer. If its retire fence is pending, the experiment keeps
that fence intact and caches the next signal value separately. It defers allocator,
CB and staged-descriptor reuse, statistics mapping and GPU timing readback until
the required completion value is observed.

OnIdle checks acquisition before UI/scene updates, diagnostic steps, automation
counters and capture clocks. An unavailable resource returns to the outer Win32
PeekMessage loop. MsgWaitForMultipleObjectsEx waits for input or a requested 1 ms
timeout before retrying; actual OS wakeup latency is not guaranteed to be 1 ms.
No window messages are dispatched recursively inside RenderFrame. No PT sample,
RR packet, camera history or presented-frame counter advances on a deferred tick.
The main submission and RR final validation/publication contracts remain intact.

Direct RunFrame callers retain a blocking safety fallback. FlushGpu, WaitForGpu,
resize/reload and shutdown can still drain GPU work explicitly. This is not a claim
that the whole application is free of CPU/GPU waits. Acquisition deferral, pending
poll and fallback-wait counters accompany RenderGraphFrame logs.

## Release ON/OFF comparison

Same chained scheduler and v1 input profile in both variants, same executable and
assets, RTX 3090 / driver 621.24. WarmGlassEdges static, 1 spp / 16 bounces / seed 7,
VSync OFF and application Debug Layer OFF. One ABBA per resolution, 150 warmup
frames and 1500 measured display intervals. No CPU-heavy build ran in this cohort.
All eight processes exited 0, with verified RR, one reset and zero rejected inputs,
outputs, cancellations or SDK failures. Both nonblocking runs at each resolution
had nonzero deferrals/pending polls and zero normal-path fallback waits. Blocking
controls had zero deferrals. Guards detected no competing RtPbrSurvey process.

Source: `90e0996e87d3669c73b6860c56d2dfe2c68346cd`.
Executable SHA256: `3ac9df173023c080624832133215fc124340cc1fc8f29f9b2a1b39e9de7fc491`.
Values below are means of two run-level rates/means.

| Resolution | Mode | Fresh RR/s | Display FPS | Render-scope fence wait ms/frame | Render-scope CPU ms/frame | Packet ms |
|---|---|---:|---:|---:|---:|---:|
| 640x360 | chained-blocking | 151.14 | 302.28 | 1.942 | 2.850 | 6.223 |
| 640x360 | chained-nonblocking | 145.32 | 290.64 | 0.000 | 0.947 | 6.456 |
| 1280x720 | chained-blocking | 50.17 | 100.33 | 8.372 | 9.458 | 19.426 |
| 1280x720 | chained-nonblocking | 49.75 | 99.50 | 0.000 | 1.069 | 19.573 |

640x360: fresh RR rate change -3.9%.

1280x720: fresh RR rate change -0.8%.

The render-scope CPU timer excludes deferred host wait and acquisition-time
readbacks in the ON mode. Its smaller number is not an equivalent reduction in
whole-thread CPU cost, elapsed frame time or GPU work. Display FPS and fresh RR/s
use wall time and include those deferred intervals. Deferred host polling can add
wakeup overhead: this cohort does not show a throughput improvement. No default
switch is justified by these results.

## Validation

The ordinary Debug recovery test passed 31 checkpoints and seven byte-identical
retained-image rejection comparisons with no D3D12 ERROR/CORRUPTION. The both-SDK
Release regression passed 22 checkpoints, including independent camera/object
translation and rotation checks, scene reload, projection/sample changes, actual
NRD execution (ten dispatches), RR restoration and Inf/huge rejection recovery.
The both-SDK Debug regression also passed all 22 checkpoints with no D3D12
ERROR/CORRUPTION and ten recorded NRD dispatches. Ordinary Debug, both-SDK Debug
and Release builds passed; ordinary Debug has the existing MSB4011 warning.
Seven CPU parser tests passed.

Blocking/nonblocking chained image comparison passed all four pairs: WarmGlassEdges
and rr-quality, each static and camera-moving. At exactly 16 verified productions,
all eight input signals and the final RR output were byte-identical (RMSE=0).
Each process recorded 16 native calls and one reset. These controlled comparisons
establish output/history equivalence for the fixtures, not subjective acceptance.

## Reproduction

```powershell
python -B Tests/PathTracing/validate_rr_recovery.py --exe bin/x64/Debug/RtPbrSurvey.exe --output build/rrq09-recovery-new --nonblocking-frame-acquisition
python -B Tests/PathTracing/validate_rr_regression.py --exe build/reconstruction-input-validation/Debug/RtPbrSurvey.exe --output build/rrq09-regression-new --nonblocking-frame-acquisition
python -B Tests/PathTracing/measure_rr_queue_baseline.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --output build/rrq09-performance-new --comparison acquisition --repeats 1 --warmup 150 --frames 1500
python -B Tests/PathTracing/compare_rr_schedules.py --exe build/reconstruction-input-validation/Release/RtPbrSurvey.exe --output build/rrq09-images-new --acquisition-comparison
```

Use new/empty output folders and an otherwise idle GPU. Raw evidence stays local
under build/rrq09-*; logs, generated fixtures, images and binaries are not committed.

## Remaining acceptance work

Actual Windows message/input-service latency has not been quantitatively measured.
The verified host deferrals establish the execution path, not user-perceived
smoothness or input-to-photon latency. Review camera/input behavior interactively
and measure message-service latency before considering a product default.

A dedicated fence-completion event combined with input wakeup is a possible next
replacement for the bounded timeout. It needs separate event ownership: reusing
the blocking wait's auto-reset event can leave stale completion signals and allow
unsafe early reuse. More backbuffers or repeated presentations alone are not an
acceptance criterion. Keep the experiment OFF by default.
