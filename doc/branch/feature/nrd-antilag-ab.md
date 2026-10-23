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

Decision: **do not adopt Anti-lag OFF**. The single A/B is complete; no second
tuning variant is scheduled. The flag remains diagnostic-only, default false.

Report: `bin/PathTracingValidation/nrd-antilag-ab-20261023/report.json`.
Status: `completed-single-antilag-ab`, 44 raw captures, no execution failures.
SHA-256: `935957cd27a442380e7c391aa0adb2e898a8c450a1ecda84d0bd0e483e47b407`.
Executable SHA-256:
`e3a9dee8bfd12df0b368d4e8ebb8f00da969d77bd8746a987cd0d64864d4d1c4`.

Debug build succeeded (23 existing LNK4199 warnings, zero errors). CTest 38/38
and Python 197/197 pass. CLI tests check default preservation and explicit
opt-in. SDK tests check that the flag changes dispatch constants, retains the
dispatch structure/pre-pass settings, and is cleared by default reinitialization.
The new default-control static SceneColor also matches the preceding delivery
NRD SceneColor bit-for-bit at frames 120/360.

## Static Result

Same previously declared blue right-cap region, 1,227 sampled pixels. RMS is
the RGB difference between frames 120/360, not a ground-truth noise estimate.

| Metric | Default | Anti-lag OFF |
| --- | ---: | ---: |
| NRD Specular temporal delta RMS | 0.463576226 | 0.463570242 |
| Composed SceneColor temporal delta RMS | 0.0224989603 | 0.0224988381 |
| Composed mean RGB at frame 360 | (0.0513561, 0.0564960, 0.0614227) | (0.0513605, 0.0565086, 0.0614360) |

The Specular delta changes by about 0.0013%, which is not meaningful evidence
of improvement. The reported persistent grain is not resolved by this variant.
This weakens Anti-lag as the dominant explanation in this fixture, but does not
rule out other per-pixel history rejection or certify the SDK's quality limit.

## Motion and Stop

Both modes pass the existing eight checkpoint history/matrix/sampling checks.
Native SceneColor is bit-identical between modes at every static and motion
checkpoint. The applied Anti-lag mode and unchanged pre-pass 30/50 are confirmed
in raw metadata. D3D12 logs contain no errors.

Illustrative existing color diagnostics, compared with same-frame *noisy*
Native rather than converged ground truth:

| Checkpoint/region | Default mean absolute RGB difference | Anti-lag OFF |
| --- | ---: | ---: |
| Stop, newly revealed (258 sampled pixels) | 0.01327924 | 0.01328402 |
| Stop, retained | 0.01276131 | 0.01276129 |
| Settle-32, retained | 0.002406543 | 0.002406681 |

No useful aggregate improvement is observed. These measurements do not prove
absence of visible ghosting or replace human review. No setting is promoted.

## Review Artifacts and Next Action

The report folder contains `static-control-diagnostic.png` and
`static-off-diagnostic.png`. Both use the same diagnostic display transform
(exposure 8, Reinhard, sRGB), not the application's final ToneMap screenshot.
`motion-control-checkpoints.gif` and `motion-off-checkpoints.gif` animate eight
checkpoints from continuous simulations at equal display intervals; they are
not real-time videos and can omit short-lived artifacts between checkpoints.

Retain defaults and preserve the highlight-grain observation as an unresolved
visual limitation. Do not mark it passed or silently begin another tuning
campaign. User acceptance of that limitation is required before finalizing the
limited delivery and completing the remaining Cornell/emissive visual checks.
