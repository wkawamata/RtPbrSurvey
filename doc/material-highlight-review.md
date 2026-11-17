# Selected material highlight review

Implementation: Material Controls checkbox, RGB/complementary color and opacity. Tone-map presentation overlay; lighting, PT accumulation and reconstruction inputs remain unmodified. Hybrid uses existing GBuffer material/object IDs. PT uses a separate depth-writing raster mask with alpha MASK, double-sided and transmission backface acceptance.

RR: overlay is suppressed when the published output epoch, camera matrix or retained instance poses do not match current geometry. Moving RR content may temporarily have no highlight. Reflections and surfaces behind transmission are excluded; this is a front-surface selection overlay.

Review: independent read-only code review requested and completed. Alpha LOD/face acceptance, complementary opacity default, RR frame alignment and incremental shader dependencies were corrected. Final reviewer reported no additional major findings.

Validation: Debug/x64 build succeeded (0 errors, 1 vcpkg MSB4011 warning). git diff --check passed. App startup and UI display observed. Actual overlay appearance, PT/Hybrid toggling and RR runtime behavior still require visual validation. Startup debug log contains resource initial-state warnings; no error was observed in the inspected log.

Status: implementation reviewed; visual acceptance pending. No commit, PR or merge performed.