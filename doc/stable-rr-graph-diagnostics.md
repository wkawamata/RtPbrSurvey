# Stable RR graph inspection and packet-aware WorkMeter

## Observed behavior

With Experimental Native Evaluate disabled, RR request alone does not perform
native SDK evaluation. With evaluation enabled, the live scheduler separates
production/conversion/admission, SDK evaluation and GPU-fence waiting while a
previous verified image stays displayed. BuildRenderPasses intentionally emits
different schedules. The former graph UI directly removed absent passes/resources
and WorkMeter filled unavailable timings with zero.

A fixed-scene Debug comparison (640x360, 96-frame capture, LogFPS 1) recorded
98 observation rows and 97 distinct completed GPU packets in each run:

| Measured packet membership | Request only | Native evaluate ON |
|---|---:|---:|
| PathTracingPass | 97 | 20 |
| RR input conversion | 97 | 20 |
| RR input admission | 0 | 20 |
| PT DLSS RR Native | 0 | 20 |
| ToneMap / ImGui | 97 | 97 |

OFF: zero SDK calls and zero completions. ON: 20 SDK calls, verified RR output;
at capture 19 outputs completed, reaching 20 in the final trace after readback.
Input-pending/output-pending/ready phase counts were 40/38/20. No rejected inputs,
cancelled packets, ERROR or CORRUPTION were recorded. This confirms staged work
and presentation-only frames in this cohort, not unexplained pass loss. It does
not establish that all latency or scheduling choices are optimal.

## Inspection model

RenderGraphNodeEditorView retains the union of observed nodes and semantic
connections. Existing pass IDs/positions and observation slots survive absent
frames. Usage-list reordering does not duplicate pins. Both physical RR outputs
remain after first observation, with current schedule membership and ping-pong
role annotations. An idle resource has no active role.

Stable mode is the default. Its timeline is explicitly an observed use span in
stable slots, not one real frame's ordering/allocation lifetime. New structural
connections can extend the span once first observed. Clear observed structure
resets the catalog; disabling stable mode shows the current scheduled graph.
Changing that mode/reset clears timing/baseline histories so distinct index
namespaces cannot contaminate each other. Saved node positions remain separate.
Actual state/barrier validation continues to use the original per-frame graph;
the catalog is never passed to execution or resource lifetime management.
Text/DOT exports and the outer graph counts remain current-schedule snapshots.

GPU checkpoints now carry monotonic packet IDs and semantic pass node IDs.
Graph timing histories map by node identity rather than changing runtime indices
and do not count the same delayed readback repeatedly. Labels say last measured.

## WorkMeter

CPU and wall-clock FPS retain their actual samples; legitimate scheduling waves
are not smoothed away. GPU/RR graphs provide three explicit missing-data modes:
Gaps (default), Hold last (stale), and Zero (execution cadence). New readback vs
stage absent from a new packet are labeled separately. Held data reports its age;
it is not a new measurement. Display-only frames therefore do not masquerade as
zero-cost SDK evaluations unless the zero-cadence mode is deliberately selected.

LogFPS with LogToFile also writes RenderGraphFrame JSON: UI/log frame, completed
GPU packet, RR input/published IDs/phase/counters, current scheduled pass names
and delayed measured pass names. Those two lists are intentionally not asserted
to describe the same submitted frame. No extra GPU pass or RR scheduling policy
was added by this UI change.

## Verification and limits

Debug build passed. RenderGraphDocumentTests passed, including alternating
capture/evaluate/display graphs, stable slots/spans, both physical outputs,
semantic pin order and explicit reset. Existing graph tests also pass.
The OFF/ON application capture checks passed with the debug window enabled.
Evidence is local under build/stable-rr-diagnostics (report.json, logs, PNGs).
Generated evidence is excluded from Git. Other apps were left running; these are
execution/correctness checks, not isolated performance measurements. Interactive
node-editor and WorkMeter visual acceptance remains pending.
