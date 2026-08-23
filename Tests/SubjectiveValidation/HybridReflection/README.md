# Hybrid Reflection Subjective Validation

This local harness presents repeatable A/B capture cases, records radio-button decisions, and exports a versioned JSON report. It intentionally keeps capture generation separate from subjective evaluation.

## Phase A Contract

- `suite.json` owns stable suite and case identifiers, capture metadata, image paths, and criterion prompts. `suite.schema.json` documents its versioned shape.
- `captures/` contains generated PNG files named by case and variant. Captures are local validation artifacts and are not committed.
- Each criterion produces `pass`, `fail`, `unable`, or `unanswered` in the exported report.
- Optional defect tags and notes preserve observations that do not fit the fixed criteria.
- Exported reports include the suite version, capture metadata, evaluation timestamp, and results keyed by stable case identifiers. `report.schema.json` defines the exported shape.

The page loads `suite.json` by default. A different suite may be selected with `?suite=relative/path.json`. Because browsers restrict JSON loading from `file://` pages, use the repeatable Phase C runner below to capture, serve, and open the evaluation suite.

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

## Stochastic Sampling Suite

Run `Start-Validation.ps1 -StochasticSampling` to use `capture-plan-stochastic.json` and `suite-stochastic.json`. Both A and B enable real stochastic rough-reflection sampling and disable synthetic temporal noise. A uses history weight `0.0`; B uses `0.9`.

The stochastic plan writes `stochastic-*.png` files so it does not overwrite the earlier synthetic-noise contract captures. The evaluator retains the same English/Japanese workflow and JSON report format, but the stochastic suite uses distinct suite and case identifiers.

For live observation, use `capture-plan-stochastic-live.json`. It holds the initial view for 120 frames, then performs three slow orbit segments including two direction changes, returns to the initial yaw, and captures one settling reference. Run A and B separately with the same plan and different history weights. Do not use this live plan as a replacement for the fixed nine-criterion HTML suite.

`-ReflectionCameraDistanceScale <scale>` applies one stable multiplier to the initial Arcball distance for reflection capture automation. A value of `0.5` renders DamagedHelmet at approximately twice the linear image size and is intended for edge diagnosis. The value does not alter interactive or non-capture camera defaults.

`-CaptureReflectionTemporalValidity` selects the temporal-history classification view while retaining the existing resolved-radiance capture setup. The colors are black for no history, blue for reprojection outside the history image, red for depth rejection, yellow for normal rejection, and green for accepted history. The classification is stored only in resolved-radiance alpha; RGB radiance and blending semantics are unchanged.

`-ReflectionCaptureDebugView <name>` selects a diagnostic view while retaining the same Hybrid Reflection capture setup. Accepted names are `pbr-params`, `normal`, `hit-material`, `evaluated-radiance`, `specular-estimate`, `resolved-radiance`, and `temporal-validity`. The PBR view maps metallic, roughness, and ambient occlusion to RGB. The option changes only the displayed capture resource; it does not change sampling, evaluation, or temporal policy.

`-ReflectionEstimatorConstantIncidentRadiance` is a default-off estimator diagnostic. It replaces only the incident-radiance input used by `ReflectionSpecularEstimate` with linear-HDR white `(1, 1, 1)`. RayQuery payloads, Evaluated/Resolved Radiance, Temporal Reflection, and LightPass remain scene-driven. HDR diagnostic reports identify the mode through `specularEstimateIncidentRadiance`.

HDR diagnostic schema version 3 also records `referenceSurfaceSample` for the ROI center pixel. For a 1x1 constant-radiance report, compare the rendered estimator mean with an independent Cook-Torrance hemisphere integral:

```powershell
python Tests\HybridReflection\integrate_constant_radiance_reference.py <report.json>
```

The script uses deterministic uniform-hemisphere midpoint integration. It is independent of the GGX importance-sampling sequence used by the shader, while matching the documented BRDF model and GBuffer-quantized surface inputs from the report.

`-ReflectionSurfaceVarianceFilter` enables the default-off 3x3 current-radiance experiment during automated capture. Samples are accepted only when visible depth, normal, roughness, and metallic are similar; near-perfectly smooth visible surfaces bypass the filter. This option does not change history rejection thresholds or history weight.

`capture-plan-material-variance-series.json` captures eight settled frames for either the evaluated- or resolved-radiance view. After capturing both variants, run `Measure-MaterialVariance.ps1` to reproduce the fixed-ROI display-space temporal-deviation report and annotated ROI image. The metric is intended for repeatable symptom comparison; it is not a measurement of the underlying HDR radiance buffers.

`Measure-HistoryWeightConvergence.ps1` compares the same series for evaluated radiance and resolved history weights `0.0`, `0.5`, `0.9`, and `0.98`. Capture variants must be named `w0`, `w50`, `resolved`, and `w98`. The report separates the full, early, and late settled windows so that lower variance can be considered alongside slow luminance settling.

`-ReflectionRejectedPixelNeighborhood` enables a default-off diagnostic policy for A/B capture. Pixels rejected by the existing depth/normal history tests use a 3x3 current-frame radiance average restricted to neighbors with matching visible depth and normal. It does not relax history rejection and does not filter accepted-history pixels.

Open `http://127.0.0.1:8765/?suite=suite-edge-stability.json` for the repeatable enlarged DamagedHelmet A/B review. A keeps the policy disabled and B enables it; both use stochastic sampling, history weight `0.9`, and camera distance scale `0.5`.

Use `capture-plan-edge-settling.json` and `suite-edge-settling.json` for the focused settling gate. They compare A/B at 1, 6, and 15 frames after camera motion stops at frame 180, with only two criteria per checkpoint.
