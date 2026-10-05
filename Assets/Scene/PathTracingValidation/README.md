# Path Tracing Validation Scenes

New reviewable correctness scenes are kept as `<number-scene_name>/scene.json`.
Each scene carries a UTF-8 Description with its purpose, expected observations,
checks and limitations. Geometry and render presets use relative paths.

01-emissive-nee-baseline: original BSDF/NEE numeric validation and retained results.
02-emissive-nee-review-20261005: saved emitter transform from interactive review.
03-unequal-emissive-panels: two colors and two areas (4:1), with RGB reference checks.
04-textured-emissive-panel: bilinear WRAP 2x2 checker emission, decoded from sRGB.
05-backfacing-emissive-panel: one-sided emission rejects the back-facing floor direction.
06-blocker-beyond-emitter: geometry beyond the light does not occlude its visibility ray.
07-empty-emitter-table: nonzero constant environment and identical BSDF/NEE/MIS fallback.
08-emissive-shadow-off: shadows OFF selects identical BSDF fallback for all three modes.
09-small-emissive-panel: 0.3m square emitter, 256 spp and four-seed reference checks.
10-large-emissive-panel: 9m square emitter, 256 spp and four-seed reference checks.
11-deep-emissive-rr: enclosed room, eight-segment BSDF/NEE/MIS and RR ON/OFF checks.

Earlier permanent fixtures remain under Assets/Scenes/PathTracingValidation so
existing runners and reports retain valid references. They have not been deleted
or migrated by this change. Local generated campaigns remain under
bin/PathTracingValidation; their raw captures/logs are not versioned assets.

Scene Editor > Refresh Scene List discovers the numbered folders in this root.
