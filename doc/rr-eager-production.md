# RR eager production after verified publication

## Scope

This first optimization removes a CPU-frame scheduling bubble. It does not remove
input admission readback, output validation, fence checks, or the single immutable
production policy. There was already no blocking CPU wait inside the live Poll;
the delay was waiting for a later display tick to start the next production.

When continuous production is active and a GPU-verified output is published,
Poll can immediately return to Idle, allowing the same display frame to trace
and admit the next input into the other output slot. The published image remains
retained and cannot be overwritten by RecordInput. SDK history Finish and output
verification still complete before this transition. Paused/noncontinuous use
retains Ready.

Completion polling now precedes UpdateFrame, so camera and instance previous-pose
history uses the newest published key. A second epoch reconciliation occurs only
if scene update invalidates history, before any new packet is recorded. Existing
GPU work must still drain before cancelled resources are reused.

## Reference mode

Normal continuous RR uses eager production. Add -PathTracingRrStrictSchedule to
retain the presentation-only Ready stage for comparison. This is a process-level
diagnostic switch, not a saved scene material/rendering parameter. First-output,
RR lifecycle and general history validation CLI modes automatically retain the
strict stage needed to pin and inspect immutable productions. Both comparison
modes share the earlier polling placement; strict is not a bitwise old binary.

## Comparison, 2026-11-16

RTX 3090, Debug, 640x360, WarmGlassEdges, 1 spp / 16 bounces, fixed camera,
180-frame capture and per-frame logs; no other RtPbrSurvey GPU app was running.
After a 30-observation warmup, the final confirmation pair recorded:

| Schedule | New verified outputs / UI intervals | Fresh output updates/sec |
|---|---:|---:|
| Strict | 30 / 151 | 7.85 |
| Eager | 38 / 151 | 9.82 |

This is approximately one update per five versus four display frames. It is a
fresh-output rate, not the application's presentation FPS or a general 25% GPU
speedup. A preceding ABBA static cohort reproduced the cadence; an eager camera
orbit cohort also passed. All compared runs verified RR output with zero input
rejections, cancellations and D3D12 ERROR/CORRUPTION. UI frame-lifecycle handling
was corrected between the initial cohort and final confirmation. The last epoch
reconciliation guard was then compiled and checked by lifecycle/root smoke tests;
it is not a separate performance cohort.

## Regression checks

CMake Debug and ordinary VS Debug builds passed. Existing RR history tests passed.
The strict general-background lifecycle validator passed 19 checkpoints including
pause/resume, Native/NRD/RR switches, in-flight cancellation, moving camera/stop,
unsupported-input rejection/recovery, projection/sampling changes and resize.
Its rejected/cancelled counters intentionally increase in injected scenarios;
there were zero D3D12 errors. It validates the retained reference path, while the
eager path has static and camera-motion execution coverage. Interactive eager
pause/resize and subjective motion quality remain review items.

The lifecycle test initially found an older late-wireframe regression: diagnostic
loops called ImGuiSystem::Render directly after UpdateUiFrame without finalizing
the UI frame. The fault was a null ImDrawData in the DX12 backend. ImGuiSystem now
tracks open frames, finalizes exactly once for direct and ordinary callbacks,
handles a missing draw-data pointer, and closes an unrendered CPU frame before
the next BeginFrame. The same lifecycle test passes with this correction.

Evidence is local under build/rr-eager-20261116: report.json (initial ABBA/motion),
final-ab/report.json and logs, lifecycle-v3/report.json (final epoch guard), and
root-final.log/png. Generated logs, captures and scripts are excluded from Git.

## Next optimization boundary

Input admission and verified output still need completed-fence polling. Combining
PT/conversion/SDK into one submission requires a separate contract for always-
numeric SDK input and rejected-packet/history recovery. This change deliberately
keeps those validation gates and adds no new queue, copies, or input-ring buffers.
