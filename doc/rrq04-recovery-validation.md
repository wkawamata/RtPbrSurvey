# RRQ-04 chained RR recovery and resize lifecycle

Status: completed on 2026-11-18. Chained mode remains opt-in.

## Result

RTX 3090, ordinary VS2022 Debug build, D3D12 Debug Layer enabled, WarmGlassEdges.
The final executable passed 31 checkpoints and seven exact retained-image comparisons.
The full suite completed normally twice after the resize fix (exit code 0).
No D3D12 ERROR/CORRUPTION was logged. Existing buffer initial-state warnings remain.
Debug build: zero errors and one existing MSB4011 warning.

WARP additionally passed 58 stamped-summary cases against optimized/reference shaders
and CPU expectations. These include high serial bits, unknown statuses, partial edge
groups and an intentionally unvalidated output pixel (including the 1x1 case).

| Scenario | Required observation | Result |
|---|---|---|
| One invalid input pixel / all invalid pixels | Sanitize before SDK; reject publication; keep verified image | passed |
| Nonfinite output pixel | Inject actual NaN RGB into the unpublished SDK output; reject it | passed |
| Incomplete output verification | Skip one validation pixel; coverage must reject publication | passed |
| Stale input / stale output summary | Previous serial with otherwise valid summary must be rejected | passed |
| SDK post-recording failure | Real SDK commands recorded, then simulated failure; drain before reuse | passed |
| Recovery after each of seven faults | One new verified image, reset=true, dirty cleared | passed |
| Pause while work is pending | Drain pending work and pin a stable verified production | passed |
| In-flight epoch cancellation | Cancelled packet is never published; next packet resets | passed |
| Native / NRD selection / RR restoration | No RR calls or RR presentation while another backend is selected | passed |
| In-flight resize and resize back | Invalidate old extent, drain, recreate, reset and recover | passed |

NRD selection here verifies RR isolation; it does not claim NRD SDK execution or glass
support. WarmGlassEdges contains transmission, for which Native fallback is expected
when NRD cannot run. Actual opaque NRD execution and broader scene/motion regressions
remain RRQ-05 work.

At the final checkpoint all 24 SDK calls are accounted for: 15 verified publications,
3 input rejections, 3 output rejections, 1 simulated SDK failure and 2 cancellations.
There is no in-flight fence at any logged checkpoint. The seven rejected outputs
preserved the previous published RGBA32F image byte-for-byte, not just its frame ID.

## Production changes

RR diagnostics now distinguish output rejection and SDK failure from input rejection.
They include retained-image age, history dirty state, production serial and slot IDs.
A rejected chained packet that still has a verified image reports
`retained-output-rejected`, rather than suggesting that Native is being displayed.
Reconstruction Details shows retained status/age and reset requirement in fixed rows
for chained mode. Normal eager mode remains the default.

The recovery CLI is explicit and process-local. It arms GPU fault injection only
inside the validator; nothing is saved into scene data or evaluation cases.
Fault and input resources are captured with the immutable production. NaN input is
injected before the RR safety envelope; unsupported original inputs never enter the
SDK as NaN. Nonfinite output corruption targets the unpublished slot only.
The simulated SDK failure is after a real successful recording, so pending commands
must still complete before resource reuse. It does not claim to reproduce every
possible NVIDIA API failure.

Conversion constants grow to 41 DWORDs, and stamped validation constants to 6 DWORDs.
A small upload resource supplies the diagnostic NaN output pixel. In normal execution
there is no diagnostic texture copy or additional full-screen pass. The new uniform
shader conditions and constants are included in later performance evaluation; this
step makes no zero-overhead or speedup claim.

## Resize failure found and corrected

Initially all 31 semantic checks completed, but process teardown faulted twice inside
NGX model DLL 170_E658703.dll (2.14.16.0), exception 0xc0000005. These runs were retained
as failures; a completion marker alone is not accepted by the wrapper.

Ordinary 256x144 and 640x360 RR runs exited normally. Stopping the recovery suite at
checkpoint 27, before resizing, also exited normally with all seven images unchanged.
Explicit SDK release at shutdown alone did not remove the full-suite failure.

The engine now calls `slFreeResources(kFeatureDLSS_RR, viewport 0)` after the existing
resize GPU drain and before replacing tagged textures. The adapter tracks whether RR
allocations were created, preserves diagnostic call counts, and allows the next SDK
evaluate to recreate its resources at the new extent. Failed release stops resize
explicitly. The same idempotent release is used before SDK shutdown.

Both full-suite runs after adding release during resize exited normally. This fixes
the observed resolution-resource lifecycle gap; the vendor DLL's internal fault
mechanism is not inferred from the crash address alone. No extra CPU wait was added
to ordinary rendering frames; resizing already drained the GPU.

The release contract was checked against the local Streamline SDK 2.12.0 guide,
ProgrammingGuide.md section for slFreeResources: pending evaluate command lists must
be flushed before release. RR-specific guide sections also identify explicit resource
release and denoiser reinitialization on extent changes.

## Reproduction and evidence

From repository root, after building Debug and checking other GPU workloads:

```powershell
python -B Tests/PathTracing/validate_rr_recovery.py --exe bin/x64/Debug/RtPbrSurvey.exe --output build/rrq04-recovery-new
```

The output directory must be new/empty. The process guard checks other RtPbrSurvey
instances before/after execution. Other desktop GPU clients remained open during
these correctness tests; no external application was terminated.

The wrapper checks process exit code, the complete ordered checkpoint list, debug
errors, exact before/after image bytes, resets, serial progression and accounting of
all SDK attempts. It preserves failed evidence and never treats early completion as
a full pass. `--max-checkpoints N` is only a bisection aid; partial runs are reported
as diagnostic-passed. Use the corresponding app CLI together with the recovery validator.

Local evidence (generated files excluded from Git):

- build/rrq04-recovery-final-20261118/report.json: final 31 checkpoints, image hashes and exit code 0.
- build/rrq04-recovery-r3-20261118/report.json: preceding full success after resize release.
- build/rrq04-before-resize-20261118/report.json: 27-checkpoint isolation, exit code 0.
- build/rrq04-recovery-20261118 and rrq04-recovery-r2-20261118: failed teardown evidence.
- build/rrq04-exit-isolation-20261118: ordinary RR runs without recovery/resize.
- build/agent-builds/rrq04-final-debug-20261118: final VS Debug build.
- build/agent-builds/rrq04-packet-tests-20261118: WARP summary-test build.

## Next

RRQ-05 broadens regression coverage: actual opaque NRD execution, scene reload,
camera/object motion and stop, projection/sampling changes, and additional numerical
boundaries. RRQ-06 measures Release performance and image differences using the same
input profile. Chained mode is not made default and no subjective quality acceptance
or PR merge is performed in RRQ-04.
