# One RELAX Anti-lag A/B

Date: 2026-10-23. User-authorized single settings experiment.

## Frozen Protocol

- A: pinned SDK defaults (acceleration 0.3, reset amount 0.5).
- B: `-NrdDisableAntilag` sets only those two amounts to zero.
- Same newly built executable and shaders for A/B; no SDK upgrade.
- Keep diffuse/specular history caps 30, fast caps 6, pre-pass 30/50,
  one sample/frame, pixel-center primary, four bounces for the glossy fixture.
- Use the original four-sphere seed-7 preset. Capture Native SceneColor,
  NRD SceneColor and NRD specular output at static frames 120/360.
- Require identical Native buffers between A/B at each checkpoint, successful
  dispatch, unchanged pre-pass/camera/sampling and explicit applied-mode metadata.
- Reuse the previous geometric blue-sphere/right-cap regions. Describe temporal
  delta RMS and mean-color changes, not new ground-truth quality acceptance.
- Replay the existing eight-checkpoint disocclusion motion/stop timeline, using
  NRD and Native SceneColor for both modes. Require identical Native buffers and
  valid history states. Compare existing revealed/retained color diagnostics.
- Prepare final static images and continuous-timeline previews for human review.
  Numeric color diagnostics alone do not establish absence of ghosting.
- Keep defaults unchanged. Do not try a second tuning variant if B fails to help.
  No high-sample reference acquisition, spatial-radius or history-cap tuning.

## Results

Pending fresh build and execution. This flag is a diagnostic opt-in, not a new
recommended default or a claim that disabling anti-lag is safe for all motion.
