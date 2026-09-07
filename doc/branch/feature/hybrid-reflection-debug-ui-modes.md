# Hybrid Reflection Debug UI Modes

## Status

Draft specification for a default compact Hybrid Reflection operator view and the existing detailed diagnostics view.

## Goals

- Make the common Hybrid Reflection controls immediately accessible.
- Preserve every current diagnostic and experimental control in Detail mode.
- Share one `HybridReflectionSettings` state between both modes.
- Keep display-mode state local to the UI.

## Mode Selection

The Hybrid Reflection section starts with a segmented `Simple | Detail` selector.

- `Simple` is the default mode.
- `Detail` preserves the current Hybrid Reflection UI and behavior.
- Switching modes does not enable, disable, reset, or copy renderer settings.
- The selected mode is persisted as a local UI preference.

## Simple Mode

Simple mode contains only these three checkboxes:

1. `Hybrid Reflection Enabled`
2. `Reflection Contribution`
3. `Stochastic Rough Sampling`

The controls map directly to the existing shared settings:

| Simple control | Existing setting |
|---|---|
| `Hybrid Reflection Enabled` | `HybridReflectionSettings::enabled` |
| `Reflection Contribution` | `HybridReflectionSettings::contributionEnabled` |
| `Stochastic Rough Sampling` | `HybridReflectionSettings::stochasticSamplingEnabled` |

When Hybrid Reflection is disabled, the latter two controls are disabled visually but retain their stored values. Re-enabling Hybrid Reflection restores the previous contribution and stochastic-sampling choices.

Recommended layout:

```text
[ Simple | Detail ]
[x] Hybrid Reflection Enabled
[x] Reflection Contribution
[ ] Stochastic Rough Sampling
```

Simple mode does not show explanatory text, tuning sliders, hit-overlay controls, temporal/spatial filtering controls, material gates, or experimental diagnostics.

## Detail Mode

Detail mode retains the current controls, including:

- Hybrid Reflection enable;
- hit overlay mode, source, and intensity;
- reflection contribution intensity and maximum distance;
- stochastic rough sampling;
- temporal history and debug-noise controls;
- rejected-pixel neighborhood processing;
- edge-aware and spatiotemporal filtering controls;
- variance-guided temporal processing;
- material gate, roughness, and metallic controls.

Detail mode edits the same settings used by Simple mode. No duplicated reflection settings are allowed.

## Work Units

### HRU-01 Shared View State

- Add the `Simple | Detail` selector to both application and runtime-owned Hybrid Reflection UI paths.
- Default new/local state to Simple.
- Persist only the selected UI mode.

### HRU-02 Simple Controls

- Render exactly the three specified controls in Simple mode.
- Reuse the current `GetHybridReflectionSettings()` and `SetHybridReflectionSettings()` path.
- Disable dependent controls without clearing their values.

### HRU-03 Detail Compatibility

- Keep the existing Detail body functionally unchanged.
- Verify mode switching preserves all advanced values.
- Remove duplicate rendering of the three common controls inside a single mode.

## Acceptance Criteria

- Hybrid Reflection opens in Simple mode by default for a new UI configuration.
- Simple mode contains only the three specified checkboxes.
- Detail mode retains all current Hybrid Reflection controls.
- Switching modes does not modify any renderer setting.
- Disabling and re-enabling Hybrid Reflection preserves contribution and stochastic-sampling values.
- Both Debug UI ownership paths expose equivalent behavior.
- Debug x64 build succeeds and the D3D12 Debug Layer reports no new errors.
