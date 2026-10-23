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

Current progress:

- [x] Scope and acceptance frozen.
- [x] Final source audit; no concrete critical renderer defect found.
- [x] Fresh build, CPU/WARP/hardware and mandatory runtime regression.
- [x] Six static review images prepared from complete committed fixtures.
- [ ] Live Cornell check.
- [ ] User visual approval for this delivery build.
- [ ] Final limited-delivery decision and handoff.

## Mandatory Acceptance

| Item | Existing evidence | Fresh delivery result |
| --- | --- | --- |
| Pinned SDK, input units/packing/material restoration | Input-semantics audit and executed GPU contract kernels | Four hardware kernel suites passed |
| Native output preserved; explicit NRD selection | Presentation/composition and reference-component tests | Raw Native preserved; five selection cases passed |
| Initialization/preparation failure uses Native fallback | Injected failure and exact raw fallback checks | Three failure modes passed |
| Reset/Pause/Resume/settings/edit/Resize lifecycle | Existing 19-stage runtime history runner | All 19 checkpoints passed |
| Continuous motion and stop do not reuse incompatible history | Continuous camera/timeline and disocclusion runners | Four motion/reset cases and 32-capture continuous timeline passed |
| CPU tests and D3D12 Debug Layer | Previous targeted and GPU passes | CTest 38/38, Python 197/197; runtime logs without errors |
| Four-sphere, Cornell and emissive scene visual regression | Earlier user approvals, limited to their recorded scope | Pending final review |
| Default OFF, noncompatible sampling rejected, known limits visible | Current presentation readiness policy | Audit and five-case runtime selection matrix passed |

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
- Fresh presentation matrix: `bin/PathTracingValidation/nrd-delivery-20261022/presentation/report.json`,
  status `passed`, ten captures across five settings. Compatible inputs select
  NRD; distance-off, random primary, four-spp and non-Radiance views preserve
  exact Native output in the existing central viewport comparison region.
- Fresh continuous timeline: `bin/PathTracingValidation/nrd-delivery-20261022/timeline/report.json`,
  status `passed-continuous-timeline`. Four resources, eight checkpoints each:
  warm-up, forward, reverse, middle, stop and 4/16/32-frame settling. Existing
  visibility/MV/matrix/reset checks pass and settling updates the output.
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

## Evidence Identity

All fresh runtime captures above use one new Debug executable:
`bin/PathTracingValidation/nrd-delivery-build-20261022/Debug/RtPbrSurvey.exe`.

| Artifact | SHA-256 |
| --- | --- |
| Executable | `70139a0f65c97a5854e9da593c0509af6c71496d6b2707b059214c15a0eb854a` |
| Runtime PT CSO | `224efba2281612c21c164c541569aa64d04e552a8b0a590842545ea8942604f7` |
| Integration report | `7da42a9fc114b82d42d530e0febd272cf93f532d8c792c0900d4217c04ff051b` |
| Presentation report | `30a6e1aa02957f2959d3193c35b9216b8459d503532346fb286563becb4eaf8e` |
| Continuous timeline report | `0a5c73fd1a34b4b2db7025a45e12c34df6baa91266a3e8acf9c1e0cd36001287` |
| Final CTest log | `6cbb29e3fd81880b442449388c6d44541a4eb00de3e633bb0c57292fb94d47da` |
| Static review image report | `e54f29e3e775f898e8174cd6a38d06e53e13ed6a605c63bd9a7cfadf17dd11dc` |
| Delivery summary report | `e72d2209184e2aa58942e0c8e5a864462e616e7d0f945dd538670765f9003ef0` |

Reports retain commands, individual capture/log hashes and runtime or raw
metadata. Hardware logs are in the delivery folder's `hardware` directory.
Operator scripts for this bounded rerun are retained beside the reports.
Old evidence builds remain unchanged; new compilation is a distinct cohort,
not silently substituted into historical quality reports.

## Supported Use and Final Human Gate

- Native-resolution PT Radiance, one sample/frame, pixel-center primary and
  denoiser distance diagnostics are required for the current diagnostic preview.
- The GUI control remains `NRD RELAX Preview (Diagnostic)`; this milestone does
  not promote the general backend-support query or enable NRD by default.
- Excluded inputs/pixels use Native fallback. View depth at or beyond 1000 is
  excluded. Arbitrary scene coverage and moving-object quality are not certified.
- Preserve four bounces for the glossy/Cornell reflection review; the previously
  observed two-bounce black reflections were path truncation, not an NRD fix.
- The final human review must confirm four-sphere highlights/reflections, Cornell
  reflections at four bounces, emissive preservation, movement/stop and ON/OFF.
  An unanswered item remains pending and prevents the final delivery verdict.
- Static review images are prepared from committed scene snapshots for
  four spheres, the large emissive panel and the emissive/Unlit control. Cornell
  is a built-in scene and still requires the live manual review.
- Native images at 120 samples and NRD at 120 elapsed frames are demonstration
  comparisons, not equal-effective-sample quality benchmarks or new references.

Prepared four-sphere review session:
`bin/PathTracingValidation/nrd-delivery-20261022/manual-glossy/session.json`.
The existing `Start-NrdVisualReview.ps1` accepts `-ExecutablePath` for this build.
Do not mark the previous user approvals as answers to this fresh-build review.

Accepted static image pack:
`bin/PathTracingValidation/nrd-delivery-20261022/review-images-r2/report.json`,
status `prepared-for-human-review`, six captures. Each fixture contains
`native.png` and `nrd.png` at 120 samples/elapsed frames. Emissive and Unlit
controls completed without D3D12 errors. This is preparation, not user approval.
The first image-pack attempt omitted companion glTF files, causing startup to
abort before rendering the emissive scene. It is a failed acquisition fixture,
not passing evidence; the retry snapshots the complete committed directories.

Summary: `bin/PathTracingValidation/nrd-delivery-20261022/report.json`, status
`automated-acceptance-passed-human-pending`. The 75 mandatory runtime captures
and six prepared static review images are distinct from the unanswered human
gate. The summary records fixed case counts and source report/asset/log hashes;
it does not turn the outstanding quality study into a pass.

No critical integration bug required a renderer fix in this delivery. The final
decision remains pending human review; do not reopen the deferred glossy
convergence study as the automatic next task. After approval, record the actual
answers and finalize the limited, default-off delivery with the known quality
issues preserved. Do not silently promote it to general quality acceptance.

## Human Observation, 2026-10-23

The user reports persistent grain around the upper-right blue sphere's
highlight with NRD enabled. Native PT with accumulation also starts noisy,
but that grain disappears after more than about 30 seconds. This is not a
visual pass and does not establish an NRD limitation or an integration defect.
The next bounded check is static frame/history continuity, without changing
filter settings or acquiring new high-sample references. The user authorized
closing the interactive app, and its exact process identity was verified before
termination. No other app was stopped.

### Static History Result

Report: `bin/PathTracingValidation/nrd-static-history-20261023/report.json`.
SHA-256: `20cdb0fb74b9679d561b2eff7aaca1716fe76d52334e86eee0024b672fdd64d3`.
Status: `passed-static-history`. Same delivery executable and original four-sphere
launch preset; no rebuild or filter-setting change. This is a fresh fixed-camera
reproduction, not telemetry recovered from the user's previous interactive pose.

| Capture frame | NRD frame index | Global reset | Recorded | Dispatches |
| ---: | ---: | --- | --- | ---: |
| 30 | 30 | false | true | 10 |
| 60 | 60 | false | true | 10 |
| 120 | 120 | false | true | 10 |
| 240 | 240 | false | true | 10 |
| 360 | 360 | false | true | 10 |

The camera/previous-camera matrices remain identical, Native sampling advances
continuously, NRD reports no backend failure, and the log has no D3D12 errors.
Repeated host/global history reset is not reproduced. This does not measure
per-pixel effective history, disocclusion/antilag rejection or the highlight's
residual noise, and does not turn the negative human observation into a pass.

The pinned `RelaxSettings` defaults retain 30-frame diffuse/specular linear
history limits and six-frame fast-history limits. Our adapter changes pre-pass
radii and reconstruction policy, not those limits. Native PT accumulation and
finite-history NRD therefore are not expected to converge identically over time.
This is a relevant distinction, not proof that the observed grain is acceptable
or solely an SDK limit. Any next diagnostic should stay confined to the blue
highlight's input/output and local-history behavior; do not reopen a broad
reference-convergence campaign. Final visual acceptance remains pending.

### Bounded Blue-Highlight Input/Output Check

Report: `bin/PathTracingValidation/nrd-highlight-20261023/report.json`.
SHA-256: `22e6c4e7ddf491a406f597787ea666d1be0e9456c5cc95af5516bddadeed45e2`.
Status: `completed-highlight-input-output-check`; quality remains ungraded.
Ten fresh captures: admitted diffuse/specular input, diffuse/specular output
and composition diagnostics at static frames 120/360. Same executable, seed 7,
four bounces and original launch preset; no filtering or SDK setting changes.

Analysis uses analytically intersected blue-sphere samples, two-pixel stride,
excluding grazing silhouette samples with view/normal cosine at most 0.2.
The right-cap subset selects world-space normal X greater than 0.5. These are
fixed geometric diagnostic regions, not a user-drawn exact grain mask or new
quality acceptance regions. Sample counts: sphere 11,467, right cap 1,227.

| Right-cap diagnostic | Input | Output |
| --- | ---: | ---: |
| Diffuse maximum RGB across two snapshots | 0.39731 | 0.08899 |
| Specular maximum RGB across two snapshots | 29.74167 | 8.79688 |
| Diffuse RGB temporal delta RMS, frames 120/360 | 0.04855 | 0.000862 |
| Specular RGB temporal delta RMS, frames 120/360 | 4.71212 | 0.46358 |

All captured buffers are finite. No sampled input RGB exceeds the SDK's
documented 250 guideline in either snapshot. Composition diagnostics select
NRD (reason zero) at every sampled sphere/right-cap point at both checkpoints;
no Native-fallback flicker is observed in those samples. Global NRD history
again advances to the declared frames with reset false and successful dispatch.
The logs contain no D3D12 errors.

Specular has the dominant measured residual temporal change in this cap. Its
two-snapshot output delta RMS is about one tenth of the input delta RMS. This
describes attenuation in these samples, not a general noise-reduction guarantee,
convergence proof or spatial-grain acceptance. Rare HDR excursions, local
history rejection between checkpoints and a differing interactive camera are
not ruled out. No concrete integration defect was identified by this check.

Do not expand into another long quality study automatically. The remaining
decision is whether the user accepts persistent highlight grain as a disclosed
limitation of the current default-off delivery, or requests one bounded RELAX
settings A/B before continuing the remaining visual scenes. No visual pass or
SDK-performance-limit classification is inferred from these measurements.

The user selected the one bounded settings A/B. See `nrd-antilag-ab.md`:
Anti-lag OFF produced no meaningful static improvement (Specular delta RMS
0.463576226 versus 0.463570242) and no useful aggregate motion/stop improvement.
It is not adopted; defaults remain unchanged. The single settings experiment
is closed. Highlight grain remains an explicit unresolved visual limitation,
not an accepted pass or proof of an NRD integration defect. User acceptance and
the remaining visual scenes are still required for the delivery decision.

### User Acceptance of the Highlight Limitation

On 2026-10-23 the user explicitly accepted persistent blue-highlight grain as
a known quality limitation after the single unsuccessful Anti-lag A/B, and
authorized proceeding to the Cornell/emissive visual checks. This is a scoped
acceptance of the disclosed limitation, not a claim that the grain was fixed,
all quality thresholds passed, or an SDK performance limit was proven.
Keep the default settings and stop further tuning in this delivery.
Cornell final visual review passed by explicit user confirmation on 2026-10-23,
with four bounces and the required pixel-center/distance diagnostics enabled.
The emissive final visual check remains pending: Scene Editor currently hides
the normal Debug window, preventing interactive NRD switching on saved scenes.
Add a separate Saved Scenes run entry before resuming that check. This delivery
is default-off and quality-limited; do not describe the pending check as passed.

### Main Integration Gate, 2026-10-23

Integrated origin/main at 6fa5bb4 in an isolated worktree for PR 101.
Preserved NRD diagnostic fields and PT timeline tests alongside main's capture
clock, encoder diagnostics and camera selection changes. Retained main's
default maximum bounce count of eight; the manual Cornell check used four.
SDK-enabled Debug x64 build passed after retrying one parallel Assets copy
failure. CTest passed 39/39, including NRD GPU contract tests and capture tests.
Python NRD tests ran 197 with no failures and seven skips because historical
generated evidence is absent in the isolated worktree. No new full visual or
long convergence campaign was performed. Emissive manual review remains pending.
