# RR evaluation and follow-up

## Deferred: transmission and refraction

- [ ] Design RR inputs for thin transmission and refracted/volume paths, including
  radiance, representative distance, surface attributes, motion and history validity.
- [ ] Check admission against the latest Native transmission implementation before
  enabling RR; do not remove unsupported-material guards without input validation.
- [ ] Evaluate glass silhouettes, objects behind glass, reflected/refracted detail,
  camera/object movement and disocclusion against a converged Native reference.


### Scene 20 baseline (2026-11-07)

- Captured Native 1 spp and 256 spp at 1920x1080 using the saved thin transmission
  comparison scene and its original preset. The asset hashes and dimensions match.
- The RR guard probe reported SDK support available, requested RR, Native output,
  incompatible inputs, and zero native evaluations. D3D12 reported no errors.
- Local evidence is under build/rr-thin-step1. The 256 spp capture is the working
  reference. Compare the clear and tinted regions, panel edges, and the area
  outside the panels separately when testing an opt-in thin transmission path.


### Scene 20 opt-in RR experiment (2026-11-07)

Run the existing comparison capture with `--rr-thin-experiment --frames 90`
and the VS Debug executable. The runner enables the CLI thin experiment and
native evaluation, captures a 1920x1080 viewport, and checks verified output,
continuous history, input rejection, and D3D12 diagnostics.

The experiment records the actual primary hit position in thin scenes and marks
a first continuation through a thin sheet separately from reflection. RR uses
the existing color and primary surface guides. Transmission supplies zero for
the reflection distance, while a reflected continuation keeps its existing
distance. Thin scenes reset history on camera or scene changes. Thick volume,
masked surfaces, and NRD retain their existing admission rules.

RTX 2080 Ti, fixed view, 1 spp per production. RR completed 18 packets, including
17 continuous evaluations, with one initial reset, zero input rejections or
cancellations, and no D3D12 errors. With the experiment flag omitted, the same
build reported incompatible inputs and zero SDK evaluations. Its fallback PNG
matched the Native 1 spp reference pixel for pixel. RR input conversion and live
distance GPU tests passed.

RGB RMSE against the Native 256 spp PNG, in 8-bit display code values:

| Region | Native 1 spp | RR |
|---|---:|---:|
| Clear panel interior | 90.22 | 5.24 |
| Blue panel interior | 74.22 | 5.65 |
| Clear panel left edge | 68.99 | 7.74 |
| Blue panel left edge | 53.74 | 5.50 |

RR accumulated temporal history in this comparison. The measured regions show
lower fixed-view image error. Detailed visual acceptance, motion, and thick glass
remain open. Local captures and diagnostics are under build/rr-thin-step2.


### Scene 37 smooth slab baseline (2026-11-08)

Captured 640x360 linear SceneColor using the saved slab preset, roughness 0,
infinite attenuation distance, and 16 bounces. The IOR 1.5 cohort contains
Native 1 spp and 256 spp. A separate IOR 1.0 capture uses 256 spp with the same
camera and lighting. All captures are finite and have no D3D12 errors.

Against the IOR 1.5 Native 256 spp image, whole-frame linear RGB RMSE is
0.02871 for Native 1 spp and 0.09949 for the IOR 1.0 control. The latter measures
the appearance change from IOR and is not a quality error threshold.
The RR guard probe reported SDK available, incompatible inputs, Native output,
and zero SDK evaluations. Evidence is under build/rr-volume-step3.

For the first slab experiment, restrict admission to smooth, nonabsorbing
qualified volumes under explicit opt-in. Use the primary interface position
and normal with the original path radiance. A reflected first continuation
keeps the existing reflection distance. A transmitted first continuation can
hit the glass exit interface at bounce 1, so supply no reflection distance for
that continuation. Confirm static output and compare the color bands and
silhouette against the IOR 1.5 reference. The IOR 1.0 capture is a control for
the position of the bands. Thick-glass RR output remains to be tested.


### Scene 37 opt-in smooth Volume RR (2026-11-08)

Enable `-PathTracingRrSmoothVolumeExperiment` and
`-EnableExperimentalNativeRayReconstruction` for the static slab test.
The experiment admits qualified Volume materials with zero roughness and
infinite attenuation distance. It uses primary interface geometry and the
original radiance. Transmitted continuations omit the reflection distance.
Camera and scene changes reset Volume RR history.

At 640x360 with the saved preset and IOR 1.5, the Debug run published 18
verified RR outputs, 17 with continuous history. There was one initial reset,
zero input rejections or cancellations, and zero captured D3D12 errors.
Without the experiment flag, or with roughness 0.35 or finite attenuation
distance 2, the output fell back to Native with zero SDK evaluations.

Display RGB RMSE against the same-build Native 256 spp capture decreased from
23.18 to 5.37 inside the slab, from 18.03 to 7.47 at the left edge, and from
20.24 to 6.03 at the right edge. Whole-frame RMSE decreased from 7.60 to 6.83.
The compared inputs are Native 1 spp and RR with 1 spp per production and
temporal history. Two color band transitions in the slab matched the Native
reference positions at x=259 and x=332 (left pixels of the strongest gradients).
The Scene 20 thin transmission RR regression also passed. Evidence is under build/rr-smooth-volume-20261108. Rough and absorbing glass
remain separate experiments. Motion and visual acceptance remain open.


### Scene 33 opt-in rough Volume RR (2026-11-08)

Use `-PathTracingRrRoughVolumeExperiment` with
`-EnableExperimentalNativeRayReconstruction`. Admission requires qualified
nonabsorbing Volume with roughness greater than zero and at most 0.35.
The experiment reuses the transmission guide policy from Scene 37.

The local Scene 33 derivative sets attenuation distance to infinity to isolate
roughness. Fixed camera, IOR 1.5, 16 bounces and 640x360. The roughness 0.35 RR
capture published 18 verified outputs with 17 continuous evaluations, one reset,
zero input rejections or cancellations, and zero D3D12 errors. The original
absorbing material and the smooth-only flag on the rough scene both produced
Native fallback with zero SDK evaluations.

Compared to roughness 0.35 Native 256 spp, the glass interior display RGB RMSE
went from 64.72 for Native 1 spp to 6.18 for RR with temporal history. Interior
mean RGB was 200.99 in the reference and 202.93 in RR on an 8-bit scale. One
checker transition averaged over x=250..329 had a 10-to-90 percent width of
3.39 px in roughness 0 Native, 35.49 px in roughness 0.35 Native, and 32.81 px
in RR. The latter two transition centers were y=144.14 and y=144.17. This
measures a single fixed-view transition. See build/rr-rough-volume-20261108
for captures, commands and regional metrics. Motion evaluation is pending.


### Transmission motion fixtures (2026-11-08)

The Saved Scenes list includes two fixed-camera motion fixtures. Use Space or
Play Animation to start and pause the six-second translation.

| Scene | Animated node | RR experiment option |
|---|---|---|
| PT RR Glass Translation | Scene 37 slab, amplitude 0.35 | `-PathTracingRrSmoothVolumeExperiment` |
| PT RR Behind Glass Translation | Scene 20 Sphere 2, amplitude 0.25 | `-PathTracingRrThinExperiment` |

The behind-glass fixture sets the clear panel roughness to zero so the sphere
is easier to track. Both use the source scene presets and assets. Add
`-EnableExperimentalNativeRayReconstruction` to enable SDK evaluation.

Debug lifecycle probes at 640x360 reached the static checkpoint in both cases
with three verified RR publications, one reset, and no input rejection or
D3D12 errors. Both timed out at the moving checkpoint after 512 frame updates,
so they did not satisfy the existing continuous-output requirement. The
experiments currently invalidate RR history on transmission scene pose changes.
The probe does not quantify moving-image ghosting or output frequency.
Logs are under build/rr-transmission-motion-20261108. Next measure publication
and reset counts through motion and stop, then compare paired Native/RR frames.
The user confirmed that the grain disappeared immediately on stopping the slab.
The restart log under build/rr-glass-restart-20261109-023056 shows verified
publications resume after motion resets, with the last observed interval
continuing at reset count 10. The input release timestamp was not logged.
Camera drag was checked visually. A paired deterministic camera test is pending.

### Glass motion visual check (2026-11-09)

The user confirmed that the black frames during camera drag disappeared after
5c1bf26. Screenshots show grain on the glass during both camera drag and slab
translation. During the observed continuous animation, over 30,000 display
frames elapsed with no new verified RR output. The sample index alternated
between 0 and 1, and the SDK reset counter stayed at 108. No D3D12 errors were
logged. This is consistent with pose changes invalidating packets before SDK
evaluation. The reset counter measures SDK evaluations with reset, so a flat
counter alone does not mean the history was preserved.

Both motion cases failed continuous RR quality acceptance before the opt-in
motion experiment below. The later user review covers that experiment. Logs are under build/rr-user-glass-clear-fix-20261109.

### Opt-in transmission motion continuity (2026-11-09)

Add `-PathTracingRrTransmissionMotionExperiment` alongside the applicable
transmission experiment flag and native evaluate flag. This reuses the
existing primary-surface motion and published-packet pose history. Empty
emitter tables can preserve history when rebuilt successfully. The experiment
adds no motion guide for the image behind a refracting surface.

At 640x360 both saved motion scenes passed all five lifecycle checkpoints.
Each produced three static outputs, eight additional moving outputs, and eight
more after pause and resume. Each had one initial reset, zero cancellations or
input rejections, and zero D3D12 errors. The retained motion and published
output captures were finite. Logs and reports are under
build/rr-transmission-continuous-20261109. The subsequent visual checks are listed in the admission decision below.

The user accepted slab translation in the interactive smooth Volume test.
They observed no visible grain on the front or side, no delay or doubling of
the refracted color bands, and no residual image after stopping. The user later accepted camera rotation
and translation in the same scene and accepted the behind-glass sphere with
slight visible smearing.

### Step 6 admission decision (2026-11-09)

RR uses frame-wide admission. The conversion validates pixel guides and the
application aggregates the result before SDK evaluation. An unsupported
material or rejected guide results in Native presentation for the frame.

| Case | Evidence | Admission |
|---|---|---|
| Opaque, supported inputs | Normal RR regression passed | Normal RR selection |
| Thin transmission, static | Scene 20 capture and numeric comparison | Thin experiment |
| Smooth nonabsorbing Volume, static | Scene 37 capture and numeric comparison | Smooth Volume experiment |
| Nonabsorbing Volume, roughness above 0 and up to 0.35 | Scene 33 derivative at 0.35 | Rough Volume experiment |
| Smooth slab translation | Verified continuous output and user visual acceptance | Volume and transmission motion experiments |
| Behind-glass object motion | Lifecycle and user visual acceptance passed with slight smearing | Thin and transmission motion experiments |
| Camera rotation and translation, smooth slab | User visual acceptance | Smooth Volume and transmission motion experiments |
| Absorbing Volume, unsupported Volume, alpha mask | Existing admission guards | Native fallback |

Deliver the tested transmission paths as opt-in experiments. The visual motion
results apply to a smooth nonabsorbing slab and one sphere behind a thin clear
panel. The material predicate covers more scenes than those fixtures, and the
motion guide tracks the primary surface. Keep the material-specific experiment
flags and the separate motion flag. Normal RR selection keeps its existing
transmission fallback. This policy applies to the whole frame.

Use the thin, smooth Volume, or rough Volume option in the corresponding
capture section together with `-EnableExperimentalNativeRayReconstruction`.
Add `-PathTracingRrTransmissionMotionExperiment` for the tested moving cases.
The rough Volume result covers a fixed view. Absorbing Volume and broader
motion qualification remain follow-up work.

The current VS Debug build passed rr-output and thin-transmission in the
existing static live suite. The former published verified RR output, and the
latter used Native fallback with zero SDK calls. Report and logs are under
build/rr-step6-regression-20261109. Performance was not remeasured in this pass.

### Release opaque regression (2026-11-10)

Built VS Release x64 at c7cc98e, based on main c29ab59. Build passed with
the existing MSB4011 warning. The same opaque rr-quality capture protocol as
2026-11-07 passed 24/24 cases on RTX 2080 Ti, with three runs per condition.
Values are means of the three run means.

| Resolution | spp | Native GPU ms, prior → current | Fresh RR outputs/s, prior → current |
|---|---:|---:|---:|
| 720p | 1 | 4.84 → 4.81 | 31.38 → 31.08 |
| 720p | 4 | 15.85 → 15.83 | 22.79 → 23.77 |
| 1080p | 1 | 10.54 → 10.59 | 18.13 → 18.20 |
| 1080p | 4 | 35.03 → 34.78 | 11.76 → 12.63 |

Native GPU timing stayed within 1 percent and RR output cadence showed no
consistent decline across the tested conditions. Session differences limit
attribution to a particular change. This cohort covers opaque regression.
Evidence is under build/reconstruction-input-validation/performance/
rr-transmission-opaque-regression-20261110/report.json.

## Dynamic objects

- [x] Add opaque translation/rotation RR scenes using the Scene35 animation mechanism.
  Saved scenes: Assets/Scenes/RrObjectRotation and Assets/Scenes/RrObjectTranslation.
- [ ] Verify object-motion input and history behavior independently of camera motion.
- [ ] Record whether animation resets history or supports continuous temporal reuse.
  A stable image obtained by resetting every animation frame is not proof of correct
  object motion vectors or continuous RR history.

Static RR scene and user acceptance records: Assets/Scenes/PathTracingValidation/rr-quality/README.md.

The new scenes are test inputs, not a claim that moving-object RR support is complete.

### Warm Glass Table camera-motion diagnosis (2026-11-13)

A 640x360 Debug run used a 20-degree deterministic orbit over the final 240
frames of a 300-frame capture. Thin and smooth-volume RR admission and native
SDK evaluation were enabled in both runs. With transmission motion OFF, static
startup completed 12 RR outputs, but motion cancelled 120 packets and the final
image used Native (awaiting-evaluation), with full-frame grain. The SDK reset
counter remained 1; it cannot diagnose cancellation starvation by itself.

With transmission motion ON, the same run completed 60 RR outputs (59 continuous),
with one initial reset, zero cancellations, zero input rejections, and verified RR
presentation at capture. The captured image did not show the full-frame grain.
Both runs captured zero D3D12 errors. Artifacts remain local under
build/rr-glass-camera-20261113/{motion-off,motion-on}; this comparison does not
qualify every interactive camera path or transmission material.

Rendering now exposes session-only RR Transmission Experiments controls and
reports cancelled/rejected counts. The Warm Glass Table startup command now
includes the transmission-motion opt-in. No material-admission guard or
experimental default was relaxed.


### Warm Glass Table integration review (2026-11-13)

The user confirmed that enabling the transmission experiment controls and native
SDK evaluation resolved camera-motion grain; Accumulate was not the cause.
The four transmission controls now round-trip through Evaluation Cases presets,
and absent command-line flags no longer overwrite restored values with false.
All 16 ON/OFF combinations passed settings serialization tests.

After incorporating main PRs #124 and #125, the Debug application built with zero
errors (one existing MSB4011 warning). A 640x360 automatic 20-degree orbit over
120 of 180 frames completed 36 verified RR outputs, 35 continuous evaluations,
one initial reset, zero cancelled packets, zero rejected inputs, and zero
captured D3D12 errors. The final presentation was rr (ready). Logs and the image
remain local under build/warm-glass-pr-validation-20261113.

The scene assets, X-ray wireframe tools, stable WorkMeter rows and collapsed
graphs, accumulation buttons, and transmission settings persistence are ready
for integration. This review does not qualify colored transmission shadows,
hollow Volume glasses, caustics, or RR output update-rate performance.
