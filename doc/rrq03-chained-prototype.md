# RRQ-03 chained PT RR prototype

Status: prototype implemented and Debug smoke passed, 2026-11-17.
This is an opt-in path. Eager remains the default; adoption and Release performance
acceptance belong to RRQ-06/07. See rrq02-chained-scheduling-design.md and
rrq03-input-safety.md for the safety profile and preceding numeric SDK probes.

## Implementation

Use `-PathTracingRrSchedule chained` to select safe input profile v1 and record
PT -> conversion -> input summary -> SDK RR -> output summary in one Direct queue
production. `safe-serial` selects the same input profile with the CPU input gate
retained. Neither flag enables native RR or relaxes material admission by itself.
Native RR evaluation and the relevant transmission experiment flags are still needed.

The prototype keeps one immutable production in flight, eight RR-only input
textures and two RR output slots. It adds no full-image copy, async compute queue,
or multi-production ring. Output remains unpublished until completed-fence polling
validates both input and output. The CPU admission polling round trip is removed;
GPU completion polling and presentation remain.

- Input/output summaries are separate 16-byte resources (mask, coverage, uint64 serial).
- Both summaries are copied into separate offsets of one 32-byte readback allocation.
- The validation shader stamps the serial, including high bits, only for this dispatch.
- The CPU requires matching serials, full coverage, zero masks, SDK success and current epoch.
- Output slot is reserved before graph construction so resource declarations match SDK writes.
- The serial is assigned when the immutable inputs are captured, before either summary is
  recorded. This is later than the design's proposed graph-construction reservation but still
  precedes all GPU work using it. Only the output slot must be known during graph construction.
- `BeginSpeculative` / `FinishSpeculative` distinguish SDK recording from confirmed admission.
  Ordinary `Finish` cannot commit a speculative production, even with GPU-output=true.
- Rejection invalidates SDK history. The next evaluate resets; a successful publication clears
  the dirty state. An earlier verified output can remain retained in the same epoch.
- Epoch changes cannot publish the old packet. SDK failures drain submitted commands before reuse.
- Existing serial modes also use stamped independent summaries; their presentation scheduling
  and default input conversion are retained and were rechecked by Debug smoke.

Chained mode is incompatible with the existing strict first-output/input-capture,
lifecycle and history CLI validators. It reports an explicit error for those
combinations. The new smoke script covers chained production; later steps must
extend lifecycle validation rather than silently falling back to strict mode.

## Diagnostics

Capture diagnostics include chained, historyDirty and finalFence in ptRr.
Raw capture metadata also includes the schedule and safe input profile version.
`[RR Packet]` logs input/admitted/output stage waits as N/A in chained mode.
`finalGpuWaitMs` measures host wall time from recording completion to publication,
including queue execution/poll cadence; it is not GPU kernel time or pure CPU blocking.
`totalMs` and evaluateCpuMs retain their meanings. The queue parser preserves N/A as
missing rather than zero. The older measure_rr_release.py parser is serial-only;
use the updated queue parser for chained logs. Formal comparative automation is
still part of RRQ-06.

## Validation

Ordinary VS2022 Debug build passed with one existing MSB4011 warning.
CPU history tests passed existing transitions plus all eight SDK/input/output
completion combinations, stale tickets, double commit and reset after rejection.
Parser tests: five passed, including N/A admission metrics for chained packets.

The stamped GPU summary test passed 48 WARP cases against the reference shader
and independent CPU expectations, including partial edge groups, status 7,
unknown/fractional statuses and high serial bits. D3D12 errors: zero.

RTX 3090 Debug smoke used 640x360, native resolution, 120-frame captures and
D3D12 Debug Layer enabled. Test processes were checked for isolation before/after
each run. Other desktop GPU clients remained open. Work5 temporarily restarted
during preparation, so the smoke did not begin until the process guard passed.
No other application was terminated.

| Case | Scene | Verified outputs at capture | Native evaluate calls | Input rejected / cancelled | Result |
|---|---|---:|---:|---:|---|
| Eager reference | WarmGlassEdges | 30 | 30 | 0 / 0 | passed |
| Safe serial | WarmGlassEdges | 30 | 30 | 0 / 0 | passed |
| Chained | WarmGlassEdges | 60 | 61 | 0 / 0 | passed |
| Chained camera orbit and stop | WarmGlassTable | 60 | 61 | 0 / 0 | passed |

All four captured native RR output with finite-output verification and zero logged
ERROR/CORRUPTION. Chained logs have no input-gpu-pending phase. Their production
graphs contain exactly one PT, conversion, input admission and RR pass in that order.
Serial reference logs do not co-schedule PT and SDK RR. The in-flight evaluate beyond
the published count is expected; it is not counted as a verified output.

These counts demonstrate scheduling cadence, not a 2x speedup. They are Debug
correctness smoke with logging/VSync and hidden windows, not Release performance
measurements. No new subjective quality acceptance is claimed. The final rebuild
contains only comment/indentation cleanup after this cohort, with unchanged logic.

## Reproduction

Build Debug and the CMake test targets, then run from the repository root:

```powershell
python -B Tests/PathTracing/validate_rr_chained.py --exe bin/x64/Debug/RtPbrSurvey.exe --output build/rrq03-chained-smoke-new
```

A new/empty output folder is required. It runs eager/safe-serial/chained plus a
chained camera orbit (8 degrees over 90 frames, capture after 120). The guard
only detects RtPbrSurvey processes; check other GPU workloads separately.
Generated captures/logs/reports stay under build and are excluded from Git.

Local evidence: build/rrq03-chained-smoke-20261117/report.json and per-run PNG/logs.
Build logs: build/agent-builds/rrq03-chained-* and rrq03-packet-tests-20261117.
The report pins the measured executable SHA256; a later rebuild can have a different hash.

## Next: RRQ-04

Add live negative-path injection and recovery tests: one/all invalid input pixels,
nonfinite/incomplete output, SDK recording failure, stale summary/epoch, pause,
backend switch, resize and draining cancellation. Verify retained image age/status,
rejection counters, no invalid publication, forced reset and resource ownership.
These broader live failure/lifecycle paths have not been accepted by this smoke.
Keep chained opt-in until these and RRQ-05 regressions pass; perform Release ABBA
and image comparisons with the same safe input profile before considering adoption.

RRQ-04 follow-up: [recovery and resize validation](rrq04-recovery-validation.md)
completed on 2026-11-18. Broader RRQ-05 regressions and adoption remain pending.
