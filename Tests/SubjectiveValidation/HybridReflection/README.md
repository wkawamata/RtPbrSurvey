# Hybrid Reflection Subjective Validation

This local harness presents repeatable A/B capture cases, records radio-button decisions, and exports a versioned JSON report. It intentionally keeps capture generation separate from subjective evaluation.

## Phase A Contract

- `suite.json` owns stable suite and case identifiers, capture metadata, image paths, and criterion prompts. `suite.schema.json` documents its versioned shape.
- `captures/` contains generated PNG files named by case and variant. Captures are local validation artifacts and are not committed.
- Each criterion produces `pass`, `fail`, `unable`, or `unanswered` in the exported report.
- Optional defect tags and notes preserve observations that do not fit the fixed criteria.
- Exported reports include the suite version, capture metadata, evaluation timestamp, and results keyed by stable case identifiers. `report.schema.json` defines the exported shape.

The page loads `suite.json` by default. A different suite may be selected with `?suite=relative/path.json`. Because browsers restrict JSON loading from `file://` pages, serve this directory over local HTTP. The repeatable capture and launch command belongs to a later orchestration phase.

Phase A does not add renderer capture behavior. Multi-frame capture within one continuous history timeline is the next phase.
