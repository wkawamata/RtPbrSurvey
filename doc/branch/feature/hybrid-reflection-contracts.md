# Hybrid Reflection Contracts

## Scope

This document is the focused contract for Hybrid Reflection pass boundaries, resources, final composition, and future temporal-history ownership. It describes current behavior and explicitly named future boundaries. It does not imply that temporal accumulation, denoising, DLSS Ray Reconstruction, Streamline integration, or PathTracing is implemented.

## Pass Flow

1. `HybridReflectionPass` performs RayQuery tracing and produces raw hit and material payloads.
2. `ReflectionEvaluatePass` consumes those payloads and evaluates current-frame, unweighted one-bounce radiance before temporal processing and final visible-surface weighting.
3. `LightPass` applies distance, visible-surface roughness, user intensity, and visible-surface Fresnel weights, then additively composites the result with deferred lighting.

A future `TemporalReflectionPass` is inserted between `ReflectionEvaluatePass` and `LightPass`:

```text
HybridReflectionPass
    -> ReflectionEvaluatePass
    -> [future TemporalReflectionPass]
    -> LightPass
```

When temporal reflection is disabled, `LightPass` consumes `ReflectionEvaluatedRadiance`. When it is enabled, `LightPass` consumes `ReflectionResolvedRadiance`.

## Data Contract

All currently implemented reflection resources are render-resolution `DXGI_FORMAT_R16G16B16A16_FLOAT` textures.

| Resource | Producer | Meaning and channels | Classification | Consumer |
|----------|----------|----------------------|----------------|----------|
| `ReflectionRayHit` | `HybridReflectionPass` | `.x`: committed hit distance; `.y`: hit flag (`1` hit, `0` miss or gated pixel); `.zw`: oct-encoded world-space hit normal. Background, miss, and gated pixels are zero. | Raw ray signal | `ReflectionEvaluatePass`, debug views, and hit overlay. Future rejection may consume all fields. |
| `ReflectionRayColor` | `HybridReflectionPass` | `.rgb`: linear hit albedo; `.a`: committed-hit validity. It is not reflected radiance or final reflected color. | Material/debug payload | `ReflectionEvaluatePass` and albedo debug views. |
| `ReflectionRayMaterial` | `HybridReflectionPass` | `.x`: metallic; `.y`: roughness; `.z`: unlit flag; `.w`: reserved. | Material/debug payload | `ReflectionEvaluatePass` and material debug views. |
| `ReflectionRayEmission` | `HybridReflectionPass` | `.rgb`: linear hit emissive after material emissive scale; `.a`: committed-hit validity. | Material/debug payload | `ReflectionEvaluatePass` and emission debug views. |
| `ReflectionEvaluatedRadiance` | `ReflectionEvaluatePass` | `.rgb`: current-frame linear HDR, unweighted one-bounce radiance; `.a`: `1`. Hits contain evaluated hit-surface lighting and emission. Miss and gated pixels contain the roughness-filtered environment fallback. | Pre-temporal evaluated radiance | Current `LightPass`, evaluated-radiance debug view, and future `TemporalReflectionPass`. |
| `ReflectionResolvedRadiance` | Future `TemporalReflectionPass` | `.rgb`: temporally processed linear HDR, unweighted one-bounce radiance; `.a`: `1`. It preserves the evaluated hit and environment-fallback semantics. | Post-temporal resolved radiance | Future temporal-enabled `LightPass`. Not implemented yet. |

The current code and this contract use `ReflectionEvaluatedRadiance`. The older `ReflectionRadiance` term refers to the same pre-temporal boundary in historical discussion and must not be interpreted as a separate resource.

Neither evaluated nor resolved radiance contains distance fade, visible-surface roughness weight, contribution intensity, visible-surface Fresnel, or final scene composition. History length, confidence, and rejection state are not packed into their alpha channel.

The final contribution is:

```text
evaluated_or_resolved_radiance
    * distance_weight
    * visible_roughness_weight
    * intensity
    * visible_surface_Fresnel
```

`LightPass` additively composites this contribution with deferred lighting. Miss and material-gated pixels use a distance weight of `1`.

## History Ownership

The engine owns two persistent, render-resolution `DXGI_FORMAT_R16G16B16A16_FLOAT` physical slots for future `ReflectionResolvedRadiance` history. Their logical roles are `historyRead` and `historyWrite`.

The CPU-side `ReflectionHistoryState` scaffold owns validity and the dedicated read index. The two persistent resource specifications, SRV/RTV slots, and role resolvers are registered. The registry creates their GPU textures lazily when a future temporal pass first declares a usage. The pass that writes the textures and advances the index is not implemented yet.

- A dedicated reflection-history index selects the roles.
- Swap-chain `m_currentFrameIndex` and `m_previousFrameIndex` do not own or select reflection history.
- `ReflectionEvaluatedRadiance` is current-frame input and is never reused as a history slot.
- The roles are exchanged only after `TemporalReflectionPass` successfully produces the current resolved result.
- A frame that does not run the temporal pass does not advance reflection history.
- Reflection history validity is independent of `m_temporalUpscalerHistoryReset`.

### Resource and Binding Names

The two physical RenderGraph resources have stable names:

- `ReflectionResolvedRadiance.0`
- `ReflectionResolvedRadiance.1`

RenderGraph read/write usages always reference these physical names. At graph construction, `readIndex` selects the read name and `readIndex ^ 1` selects the write name. A logical RenderGraph resource name such as `ReflectionResolvedRadiance.HistoryRead` must not dynamically resolve to different physical textures because the current resource-state tracker stores state by resource name; changing the physical target behind one name would make tracked and actual D3D12 states diverge after a role exchange.

Pass bindings may use semantic role names because their resolver callbacks are evaluated when commands are recorded:

- `ReflectionResolvedRadianceHistorySrv` resolves to the SRV for `readIndex`;
- `ReflectionResolvedRadianceCurrentSrv` resolves to the SRV for `readIndex ^ 1`;
- `ReflectionResolvedRadianceCurrentRtv` resolves to the RTV for `readIndex ^ 1`.

The selected indices remain constant throughout one frame. The future temporal pass reads the physical history slot only when history is valid and writes the physical current slot. `LightPass` reads that same current slot. The role exchange occurs after successful temporal output production and cannot happen while the frame graph is being executed.

### Descriptor and RTV Inventory

Each physical slot owns one persistent SRV and one persistent RTV. The history pair therefore adds exactly:

- two shader-visible SRV descriptors;
- two RTV descriptors;
- no UAV descriptors in the initial full-screen render-target implementation.

The descriptors are allocated once with the render-size resources and recreated in place when those resources are recreated. Separate descriptors are preferred over rewriting a shared descriptor during ping-pong because they keep bindings stable and avoid descriptor mutation while earlier submitted GPU work may still reference them.

## History Reset

A reset logically invalidates reflection history; it does not require clearing both physical textures. On the first temporal reflection frame after reset, the pass must not sample old history. It produces the current `ReflectionResolvedRadiance` without history, then marks that result valid and makes it the next `historyRead` slot. A pending reset is cleared only after successful output production. History remains invalid while temporal reflection is disabled.

Hard-reset events are:

- initial history allocation or reallocation;
- output-size or render-size changes;
- scene reload, scene close, or rendering-path changes;
- an explicit camera cut, teleport, or camera-preset change;
- Hybrid Reflection or temporal reflection enable-state changes;
- material, lighting, or environment changes;
- Material Gate or another ray-selection setting change that changes evaluated radiance.

Normal camera movement is not a hard reset; future reprojection handles it. Camera cuts use an explicit engine reset signal rather than a matrix-difference heuristic.

Post-resolve controls do not reset history: distance fade, maximum distance, visible-surface roughness weight, contribution intensity, hit overlay, exposure, tone mapping, and debug-view selection.

Future rejection may consume current and previous depth or reconstructed position, visible-surface normal and roughness, motion vectors, reflection hit flag, hit distance, and hit normal. Reprojection, rejection thresholds, auxiliary history resources, accumulation shaders, and debug views are outside this contract phase.

## Comparison Boundary

PathTracing comparison must name the signal boundary being compared. `ReflectionEvaluatedRadiance` is pre-temporal and pre-final-weighting. `ReflectionResolvedRadiance` is post-temporal but retains the same unweighted semantics. The final LightPass contribution is weighted and embedded in the lit scene, so it is a different comparison boundary.

DLSS Ray Reconstruction may motivate a different input contract later. No DLSS RR or Streamline SDK type or backend is connected by this document.
