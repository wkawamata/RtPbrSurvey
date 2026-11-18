# RRQ-07 scheduling decision and review scope

Status: implementation and automated acceptance completed on 2026-11-19.
Decision: retain chained as an explicit experiment; keep eager as the default.
See [RRQ-06 evidence](rrq06-performance-and-parity.md) for the controlled comparison.

## Accepted behavior

Chained records PT, safe input conversion, input validation, native RR evaluation,
output validation and both stamped summaries in one ordered GPU submission. It
removes the intermediate CPU admission round trip. The live poll was already
nonblocking; this is not the removal of a per-frame WaitForGpu call.

The CPU publishes only after the final fence and both summaries pass. One immutable
production is in flight, with separate writing and retained output slots. Rejected
work never replaces the verified image; the next valid evaluate resets SDK history.
Epoch changes drain in-flight work and invalidate stale presentation. Resize frees
native RR resources after the existing GPU flush and before replacing tagged textures.

Existing native-evaluate opt-in and material/transmission admission remain required.
The new v1 safety envelope is conservative application policy, not a vendor guarantee
that arbitrary invalid signals are safe. No async-compute queue, multi-packet ring,
or GPU-only publication is introduced.

## Why the default remains eager

The comparison isolates scheduling: safe-serial and chained share the same v1 inputs.
A fresh-output-rate gain does not by itself establish a better default. Display FPS,
CPU frame time, packet latency, retained image age and GPU work must also be assessed.
Controlled input/output parity establishes numeric equivalence for the tested fixtures;
it does not replace subjective moving-glass acceptance or cover arbitrary HDR values,
large depth ranges, thin/smooth/rough transmission and every scene configuration.

The v1 ranges (including the 65504 limit and motion bounds) can reject scenes accepted
by the original eager path. Keep the process-local selector out of saved Evaluation
Cases. Evaluate wider scene compatibility and interactive quality before defaulting it.

## Review and integration

The branch integrates origin/main through 14c3d47 (#129, #130 and #131), including the
VSync-off presentation fix before final performance measurement. The RenderGraph editor
keeps main's controls and optional detail pane together with stable observed structure.
Integration exposed an uninitialized lazy-depth SRV when ToneMap bound its complete
GBuffer table in PT; initializing that slot as a null SRV fixes startup and resize
without forcing an unused depth allocation. The subsequent Debug recovery suite passed.

RRQ-04/05 establish rejection recovery, immutable retained images, backend changes,
scene reload, sampling/projection resets and camera/object motion. RRQ-06 records the
same-input image comparisons and Release measurements. Raw logs, captures, generated
fixtures and reports remain local under ignored build folders; no images are published.

Prepare the PR for review with eager unchanged. Merging and any future default change
are separate decisions. No automatic scene-data migration is needed.

## Remaining work after this series

- Broaden compatibility beyond the v1 envelope and tested fixture ranges.
- Review subjective moving-glass quality and interactive latency at target resolutions.
- Compare a future presentation policy against the measured display-FPS tradeoff.
- Consider multiple productions or GPU-side validated presentation only as a separate
  ownership/history design with its own negative-path tests.
