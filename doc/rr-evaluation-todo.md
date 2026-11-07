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

## Dynamic objects

- [x] Add opaque translation/rotation RR scenes using the Scene35 animation mechanism.
  Saved scenes: Assets/Scenes/RrObjectRotation and Assets/Scenes/RrObjectTranslation.
- [ ] Verify object-motion input and history behavior independently of camera motion.
- [ ] Record whether animation resets history or supports continuous temporal reuse.
  A stable image obtained by resetting every animation frame is not proof of correct
  object motion vectors or continuous RR history.

Static RR scene and user acceptance records: Assets/Scenes/PathTracingValidation/rr-quality/README.md.

The new scenes are test inputs, not a claim that moving-object RR support is complete.
