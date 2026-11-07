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

## Dynamic objects

- [x] Add opaque translation/rotation RR scenes using the Scene35 animation mechanism.
  Saved scenes: Assets/Scenes/RrObjectRotation and Assets/Scenes/RrObjectTranslation.
- [ ] Verify object-motion input and history behavior independently of camera motion.
- [ ] Record whether animation resets history or supports continuous temporal reuse.
  A stable image obtained by resetting every animation frame is not proof of correct
  object motion vectors or continuous RR history.

Static RR scene and user acceptance records: Assets/Scenes/PathTracingValidation/rr-quality/README.md.

The new scenes are test inputs, not a claim that moving-object RR support is complete.
