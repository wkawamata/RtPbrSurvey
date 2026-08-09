# Hybrid Reflection Subjective Validation

This local harness presents repeatable A/B capture cases, records radio-button decisions, and exports a versioned JSON report. It intentionally keeps capture generation separate from subjective evaluation.

## Phase A Contract

- `suite.json` owns stable suite and case identifiers, capture metadata, image paths, and criterion prompts. `suite.schema.json` documents its versioned shape.
- `captures/` contains generated PNG files named by case and variant. Captures are local validation artifacts and are not committed.
- Each criterion produces `pass`, `fail`, `unable`, or `unanswered` in the exported report.
- Optional defect tags and notes preserve observations that do not fit the fixed criteria.
- Exported reports include the suite version, capture metadata, evaluation timestamp, and results keyed by stable case identifiers. `report.schema.json` defines the exported shape.

The page loads `suite.json` by default. A different suite may be selected with `?suite=relative/path.json`. Because browsers restrict JSON loading from `file://` pages, serve this directory over local HTTP. The repeatable capture and launch command belongs to a later orchestration phase.

Phase A itself does not add renderer capture behavior. Continuous-timeline capture is defined by the Phase B contract below.

## Phase B Capture Plan

`capture-plan.json` defines a continuous camera timeline and exact capture frames. Camera yaw is relative to the initial Arcball yaw and is linearly interpolated between keyframes. Before the first keyframe and after the last keyframe, the nearest keyframe value is held.

The seeded plan holds the camera still for 120 warm-up frames before starting the orbit so startup history formation is not mistaken for motion behavior.

Each capture path must be relative to the plan directory, end in `.png`, avoid parent traversal, and contain exactly one `{variant}` token. The command-line variant accepts letters, digits, `-`, and `_`. This prevents A and B runs from silently overwriting each other.

The plan is loaded with `-ReflectionCapturePlan <path>` and `-ReflectionCaptureVariant <variant>`. A plan implies the resolved-radiance capture mode. It remains mutually exclusive with the legacy `-CapturePath`. `-ExitAfterCapture` exits only after every planned PNG has completed.

Only one screenshot may be in flight. If the previous screenshot has not completed by the next requested frame, the plan fails instead of silently capturing a later frame. Capture frames should therefore have deliberate spacing.

## Phase C Repeatable Run

Run `Start-Validation.ps1` from PowerShell. By default it builds Debug x64, captures A and B sequentially, verifies every planned PNG, creates ignored `current-suite.json` metadata, starts a loopback-only server, and opens the evaluator. `-SkipBuild`, `-SkipCapture`, and `-NoBrowser` support focused reruns.

The evaluator saves reports directly under ignored `reports/`. If the report endpoint is unavailable, it falls back to a browser JSON download. Run `Stop-Validation.ps1` when evaluation is complete. The stop script uses the server-specific token recorded under `reports/`; it does not terminate a process by an unverified PID.

Recapturing replaces the six generated PNG files while stable suite, case, and criterion identifiers remain unchanged. Committed suite data stays reproducible; run-specific commit, UTC timestamp, plan hash, and working-tree state live only in `current-suite.json` and exported reports.

The evaluator can switch between English and Japanese without recreating controls or losing current radio, defect, or note state. Exported reports record the active locale. DamagedHelmet captures are displayed at two-times scale around the image center so the reflection details occupy a useful review area; this is a presentation-only crop and does not alter captured pixels.
