# Path Tracing Validation Scenes

New reviewable correctness scenes are kept as `<number-scene_name>/scene.json`.
Each scene carries a UTF-8 Description with its purpose, expected observations,
checks and limitations. Geometry and render presets use relative paths.

01-emissive-nee-baseline: original BSDF/NEE numeric validation and retained results.
02-emissive-nee-review-20261005: saved emitter transform from interactive review.

Earlier permanent fixtures remain under Assets/Scenes/PathTracingValidation so
existing runners and reports retain valid references. They have not been deleted
or migrated by this change. Local generated campaigns remain under
bin/PathTracingValidation; their raw captures/logs are not versioned assets.

Scene Editor > Refresh Scene List discovers the numbered folders in this root.
