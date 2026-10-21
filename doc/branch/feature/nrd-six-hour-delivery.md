# NRD RELAX Bounded Delivery

Start: 2026-10-22 01:49 JST. Time budget: six hours, until 07:49 JST.
Baseline: `924eace`, branch `codex/pt-denoiser-input-contract`.

## Scope Freeze

Deliver an explicitly enabled, native-resolution NRD RELAX path with mandatory
integration checks and documented limitations. Keep the default OFF. Do not
reinterpret this milestone as unrestricted scene-quality acceptance.

Do not add high-sample references, acquisition optimizations, new denoisers,
new diagnostic features, estimator changes or broad refactoring. Keep the
existing 3% reference qualification and 5% diagnostic bias thresholds unchanged.
Only fix concrete integration defects; budget at most two focused fixes.

Existing user edits in `App/SceneEditorUi.cpp` and the large-emissive scene,
plus local editor/generated files, are not part of this delivery. Preserve them.
Use a separate build and copied validation fixtures; do not overwrite frozen
evidence binaries, shaders, captures or reports.

## Schedule

| Elapsed | Work | Exit condition |
| --- | --- | --- |
| 0:00-0:30 | Freeze acceptance and map existing evidence | This checklist and explicit unresolved quality issues |
| 0:30-2:00 | Final integration audit, at most two focused fixes | Review input units, history, fallback and GPU resource ownership |
| 2:00-3:15 | Fresh Debug build and mandatory regression | CPU/GPU tests and live lifecycle succeed |
| 3:15-4:15 | Bounded scene regression | Four-sphere, Cornell and emissive-panel static/motion/settle checks |
| 4:15-4:45 | User visual check | Record actual answers, never infer approval |
| 4:45-5:30 | Fix reserve and focused rerun | No unresolved critical integration failure |
| 5:30-6:00 | Documentation and commits | Explicit limited-delivery verdict or precise blocker |

These are maximum allocations, not required waiting periods. Shorten completed
phases and use the reserve only when needed. A critical defect is a release
blocker, not a known-quality exception. If a larger redesign is required at
the two-hour gate, stop expansion and report the minimal blocking work.

## Mandatory Acceptance

| Item | Existing evidence | Fresh delivery result |
| --- | --- | --- |
| Pinned SDK, input units/packing/material restoration | Input-semantics audit and executed GPU contract kernels | Four hardware kernel suites passed |
| Native output preserved; explicit NRD selection | Presentation/composition and reference-component tests | Raw Native preserved; selection matrix running |
| Initialization/preparation failure uses Native fallback | Injected failure and exact raw fallback checks | Three failure modes passed |
| Reset/Pause/Resume/settings/edit/Resize lifecycle | Existing 19-stage runtime history runner | All 19 checkpoints passed |
| Continuous motion and stop do not reuse incompatible history | Continuous camera/timeline and disocclusion runners | Four static/motion/reset cases passed; full timeline pending |
| CPU tests and D3D12 Debug Layer | Previous targeted and GPU passes | CTest 38/38, Python 197/197; runtime logs without errors |
| Four-sphere, Cornell and emissive scene visual regression | Earlier user approvals, limited to their recorded scope | Pending final review |
| Default OFF, noncompatible sampling rejected, known limits visible | Current presentation readiness policy | Audit passed; runtime selection matrix running |

Prior passes are historical evidence, not fresh results for the delivery build.
Final evidence must name the executable/shader identity, commands, fixture,
runtime state and captured results. An omitted or failed test is not a pass.

## Unresolved Quality Issues

- The smooth-dielectric mirror-proxy Diffuse reference pair remains unqualified
  at 5.0129% NRMSE after 32768 samples. Keep its absolute Diffuse/joint verdict
  inconclusive; no higher-sample acquisition in this delivery.
- Total mean-color bias exceeds the unchanged 5% target in three previously
  assessed profile/checkpoint rows. Preserve those failures.
- Specular mean-color bias passing does not prove spatial-detail quality;
  historical output NRMSE remains substantial.
- Independent radiance/distance sampling and spatial pre-pass mixing remain
  hypotheses, not proven integration defects or established NRD limits.

See `nrd-refined-component-comparison.md` and `nrd-fast-reference-pilot.md`.
These limitations prevent a universal quality claim, but do not by themselves
prove a critical integration defect. The final verdict must distinguish the two.

## Execution Log

- Scope frozen. No new reference acquisition or filter tuning is scheduled.
- Separate Debug build succeeded with zero errors and 23 LNK4199 warnings.
- CTest 38/38 and NRD Python tests 197/197 pass (194 existing, three process-runner
  checks). The first sandboxed
  CTest attempt failed three atomic-save tests; the identical build passed all
  tests outside that filesystem restriction. No renderer fix was made for it.
- Fresh integration report: `bin/PathTracingValidation/nrd-delivery-20261022/integration-r4/report.json`,
  status `passed-integration-rerun`. All 19 lifecycle captures and 14 standalone
  captures passed; Native output is preserved and failure presentation is exact.
- Four fresh hardware suites passed: 28,672 sampling vectors, eight packing
  cases, eight material-factor round trips and eight input-conversion cases.
  All hardware logs report no D3D12 errors.
- Final presentation matrix and continuous timeline regression are in progress.
- User visual confirmation is pending.

## Final Source Audit

No concrete critical integration defect was identified in this bounded review.
No renderer/shader behavior or filter default has been changed for delivery.

| Topic | Code reviewed | Observation |
| --- | --- | --- |
| SDK and signal encoding | `NrdRelaxSdkAdapter::Initialize`, pinned checkout | Version 4.17.3, normal/roughness encodings checked; checkout remains `792eff196afdd350fd9c3f862119017ccb438a0e` |
| Input and restoration | `NrdRelaxInputConversion.hlsli`, `NrdRelaxComposition.hlsli` | Pinned material factors removed/restored once; inactive lobes and Native fallback explicitly guarded |
| Units and range | `PrepareFrame`, `NrdRelaxInputAdmission.hlsli` | NDC motion scales `(0.5,-0.5,0)`; positive view depth below 1000 accepted, farther samples excluded rather than composed as denoised black |
| History | `SetPathTracingSettings`, `InvalidatePathTracingHistory`, `ExecuteNrdRelaxPass` | Sample-mode/settings/edit changes invalidate output; previous-matrix mismatch restarts history; continuous camera policy remains explicit |
| Preparation failure | `NrdRelaxD3D12::Record` | Allocations, SDK scheduling and descriptor writes finish before command recording; only pre-recording healthy-device errors are recoverable |
| Resource ownership | `Record`, `ApplyResize`, `DestroyFrameResources`, frame fence wait | Per-frame descriptors/constants; external states restored; GPU wait precedes pool retirement and shutdown |
| Final selection | `ResolvePathTracingNrdPresentation`, `BuildRenderPasses` | Default Native, explicit compatibility/readiness gates; backend failure selects Native and stops NRD passes |

Runtime rerun: `Tests/PathTracing/validate_nrd_delivery.py`. It reuses existing
history, presentation, failure and MV assessment functions on an explicit new
executable. Frozen helpers and historical evidence builds are not modified.

The first two operator-runner attempts incorrectly compared an absent/stale
NRD debug texture to a later Native frame. Preserve those failed reports as
runner-development evidence, not renderer defects or passing data. The proper
contract checks final viewport Native selection after initialization/late
failure, and raw equality only on the preparation-failure frame itself. A third
attempt completed these checks but failed to decode a Japanese fixture using
the process locale. The final runner explicitly uses UTF-8 for JSON fixtures.
