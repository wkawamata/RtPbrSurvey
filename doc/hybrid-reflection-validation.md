# Hybrid Reflection Validation Flags

These flags support deterministic Hybrid Reflection validation captures. `-ReflectionCapturePlan` implies the resolved-radiance capture mode and is mutually exclusive with `-CapturePath`.

| Flag | Argument | Description |
|------|----------|-------------|
| `-CaptureReflectionResolvedRadiance` | None | Force Deferred rendering, Hybrid Reflection, and the resolved-radiance debug view for an automated capture. |
| `-ReflectionTemporalWeight` | Weight, e.g. `0.9` | Override the experimental reflection history weight for an automated resolved-radiance capture. |
| `-ReflectionOrbitDegrees` | Degrees, e.g. `20` | Apply a deterministic horizontal Arcball orbit ending on the automated capture frame. |
| `-ReflectionOrbitFrames` | Frame count, e.g. `30` | Spread the automated reflection orbit over this many rendered frames. |
| `-ReflectionTemporalNoiseStrength` | Strength, e.g. `0.5` | Inject deterministic zero-mean luminance noise into the current resolved-radiance input for temporal validation. |
| `-ReflectionCapturePlan` | Plan path, e.g. `Tests\\SubjectiveValidation\\HybridReflection\\capture-plan.json` | Load a versioned continuous-timeline Hybrid Reflection multi-capture plan. |
| `-ReflectionCaptureVariant` | Variant token, e.g. `a` | Replace the required `{variant}` token in every planned capture path. Accepts letters, digits, `-`, and `_`. |
