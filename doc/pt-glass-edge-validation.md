# Glass edge fixture validation

Added Saved Scene **PT Warm Glass Edges**, derived from the accepted thin-shadow
scene. Existing scenes remain unchanged. This is the first implementation of
the overall plan item 4 (edge/thickness presentation), not caustics stage 4.

## Geometry

Rounded tumbler rims/base transitions replace the coarse profile; radial segments
increase from 48 to 128. Panel bevel radii are 0.004 and 0.025, with unchanged
outer dimensions. Optical modes, lighting, camera and exposure are retained.
Thin tumblers still use thin-surface optics, not hollow Volume transport.

The single solid slab is an imported convex Volume. Tangent half-space clipping
keeps its faces planar; face-center triangulation avoids near-collinear fans.
Placement is baked into RH asset positions to avoid a second float transform.
Three quarter-edge steps pass the existing asset and world convex guards.
Earlier warped/finer unstable fixtures failed those guards and were replaced;
no guard or tolerance was relaxed. The standard-library generator validates
welded manifold incidence/orientation, unit normals and positive volume.
Regeneration is deterministic.

## Accepted checks, 2026-11-14

- Normal Debug app build passed (one existing MSB4011 warning); scene-probe build
  passed. SceneDocumentBuilderTests full suite passed. The executable now also
  accepts scene.json to report whole-scene import/placement errors before GPU use.
- All four individual glTFs and complete normal/dense scenes passed importer and
  placement guards. Native captures: 960x540, 512 accumulated samples, fixed seed.
- Dense comparison: 256 radial segments and four quarter-edge steps. Display RGB
  mean absolute differences in glass / thin-panel / slab ROIs: 0.00492 / 0.00043 /
  0.01112 on a 0–1 scale. These include sampling noise and geometry changes and
  are not convergence thresholds or linear-light energy checks. Narrow slab
  highlights remain sensitive to tessellation.
- Screenshots show reflected bands on slab edges and a rolled tumbler mouth.
  Smooth thin/Volume sampling uses geometric normals, so smooth vertex normals
  alone cannot remove tessellation effects.
- RR automated 20-degree orbit: verified output, 36 completed / 35 continuous,
  one reset, zero cancelled and zero rejected. Final capture is after motion
  stops; this is execution smoke coverage, not subjective motion acceptance.
- Release capture logs contain no ERROR/CORRUPTION; their debug layer is off.
- Final normal Debug capture completed, debug layer confirmed enabled, with no
  ERROR/CORRUPTION. A buffer-initial-state warning was present; no zero-warning
  claim is made.

Local evidence: `build/glass-edges-20261114/review-v3/report.json`, rounded/dense/
RR PNGs and `density-comparison.json`; the report reuses the successful same-
settings baseline capture from review-v2. Debug evidence: `debug-final.log/png`.
Generated evidence and failed early captures remain outside Git acceptance.

## User review and limits

Compare **PT Warm Glass Thin Shadows** with **PT Warm Glass Edges** in Native
first, then RR. Inspect the mouths, panel edge reflections, camera orbit and stop.
User visual acceptance is pending. Hollow Volume transport, Volume transmission
shadows, rough transmission shadows, absorption and caustics remain separate
roadmap items. No performance acceptance or optimal tessellation claim is made.
