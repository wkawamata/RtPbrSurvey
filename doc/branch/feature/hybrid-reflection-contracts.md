# Hybrid Reflection Contracts

## Scope

This document is the focused contract for Hybrid Reflection pass boundaries, resources, final composition, and temporal-history ownership. It describes current behavior and explicitly named future boundaries. A motion-reprojected history-blend experiment with minimum visible-depth/normal rejection exists, but production temporal accumulation, denoising, DLSS Ray Reconstruction, Streamline integration, and PathTracing are not implemented.

Implementation progress and validation results are recorded in the [Hybrid Reflection History Work Log](hybrid-reflection-history-worklog.md).

## Pass Flow

1. `HybridReflectionPass` performs RayQuery tracing and produces raw hit and material payloads.
2. `ReflectionEvaluatePass` consumes those payloads and evaluates current-frame, unweighted one-bounce radiance before temporal processing and final visible-surface weighting.
3. `LightPass` applies distance, visible-surface roughness, user intensity, and visible-surface Fresnel weights, then additively composites the result with deferred lighting.

`TemporalReflectionPass` is inserted between `ReflectionEvaluatePass` and `LightPass`:

```text
HybridReflectionPass
    -> ReflectionEvaluatePass
    -> TemporalReflectionPass
    -> LightPass
```

On the first frame after invalidation, `TemporalReflectionPass` copies `ReflectionEvaluatedRadiance` into the current `ReflectionResolvedRadiance` slot without binding history. Once history is valid, it declares the previous resolved slot as a read-only input and applies the experimental blend below. The default history weight is zero, preserving identity behavior. `LightPass` consumes the resolved output when reflection contribution is enabled.

```text
resolved_rgb = lerp(current_evaluated_rgb, previous_resolved_rgb, history_weight)
```

`history_weight` is clamped to `[0, 0.98]`. Valid history is sampled at the nearest pixel selected by the motion-vector convention below only when bounds, previous-depth, and previous-normal tests pass. The current one-ray signal is deterministic in a static scene, so measured static noise reduction is negligible. The control remains useful for correspondence and future stochastic-input experiments, and zero remains the safe default while rejection quality is evaluated.

## Data Contract

Reflection payload and radiance resources are render-resolution `DXGI_FORMAT_R16G16B16A16_FLOAT` textures. Auxiliary history uses the formats listed below.

| Resource | Producer | Meaning and channels | Classification | Consumer |
|----------|----------|----------------------|----------------|----------|
| `ReflectionRayHit` | `HybridReflectionPass` | `.x`: committed hit distance; `.y`: hit flag (`1` hit, `0` miss or gated pixel); `.zw`: oct-encoded world-space hit normal. Background, miss, and gated pixels are zero. | Raw ray signal | `ReflectionEvaluatePass`, debug views, and hit overlay. Future rejection may consume all fields. |
| `ReflectionRayColor` | `HybridReflectionPass` | `.rgb`: linear hit albedo; `.a`: committed-hit validity. It is not reflected radiance or final reflected color. | Material/debug payload | `ReflectionEvaluatePass` and albedo debug views. |
| `ReflectionRayMaterial` | `HybridReflectionPass` | `.x`: metallic; `.y`: roughness; `.z`: unlit flag; `.w`: reserved. | Material/debug payload | `ReflectionEvaluatePass` and material debug views. |
| `ReflectionRayEmission` | `HybridReflectionPass` | `.rgb`: linear hit emissive after material emissive scale; `.a`: committed-hit validity. | Material/debug payload | `ReflectionEvaluatePass` and emission debug views. |
| `ReflectionEvaluatedRadiance` | `ReflectionEvaluatePass` | `.rgb`: current-frame linear HDR, unweighted one-bounce radiance; `.a`: `1`. Hits contain evaluated hit-surface lighting and emission. Miss and gated pixels contain the roughness-filtered environment fallback. | Pre-temporal evaluated radiance | Current `LightPass`, evaluated-radiance debug view, and future `TemporalReflectionPass`. |
| `ReflectionResolvedRadiance` | `TemporalReflectionPass` | `.rgb`: resolved linear HDR, unweighted one-bounce radiance; `.a`: `1`. It preserves the evaluated hit and environment-fallback semantics. With valid in-bounds history and nonzero experimental weight, RGB is a nearest-sampled motion-reprojected exponential history blend. | Resolved-radiance boundary | `LightPass` and resolved-radiance debug view. Future production temporal processing remains inside this boundary. |
| `ReflectionHistoryDepth` | `TemporalReflectionPass` | `R32_FLOAT` current visible-surface device depth copied into the matching history slot. | Rejection history | Next `TemporalReflectionPass`. |
| `ReflectionHistoryNormal` | `TemporalReflectionPass` | `R16G16B16A16_FLOAT`; `.xyz` is normalized world-space visible-surface normal and `.w` is `1`. | Rejection history | Next `TemporalReflectionPass`. |

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

The engine owns two persistent render-resolution physical slots each for `ReflectionResolvedRadiance`, `ReflectionHistoryDepth`, and `ReflectionHistoryNormal`. Their shared logical roles are `historyRead` and `historyWrite`.

The CPU-side `ReflectionHistoryState` owns validity and the dedicated read index. The two persistent resource specifications, SRV/RTV slots, and role resolvers are registered. The registry creates each GPU texture lazily when it first becomes the current write target. After a frame containing `TemporalReflectionPass` is submitted to the direct queue, the engine promotes that output to valid history and exchanges the read/write roles. Valid history is bound and declared as a read-only RenderGraph dependency. The current RGB weighting uses nearest motion-vector reprojection and bounds rejection.

- A dedicated reflection-history index selects the roles.
- Swap-chain `m_currentFrameIndex` and `m_previousFrameIndex` do not own or select reflection history.
- `ReflectionEvaluatedRadiance` is current-frame input and is never reused as a history slot.
- The roles are exchanged only after the command list containing `TemporalReflectionPass` is submitted to the direct queue.
- A frame that does not run the temporal pass does not advance reflection history.
- Reflection history validity is independent of `m_temporalUpscalerHistoryReset`.

### Resource and Binding Names

The two physical RenderGraph resources have stable names:

- `ReflectionResolvedRadiance.0`
- `ReflectionResolvedRadiance.1`
- `ReflectionHistoryDepth.0` / `.1`
- `ReflectionHistoryNormal.0` / `.1`

RenderGraph read/write usages always reference these physical names. At graph construction, `readIndex` selects the read name and `readIndex ^ 1` selects the write name. A logical RenderGraph resource name such as `ReflectionResolvedRadiance.HistoryRead` must not dynamically resolve to different physical textures because the current resource-state tracker stores state by resource name; changing the physical target behind one name would make tracked and actual D3D12 states diverge after a role exchange.

Pass bindings may use semantic role names because their resolver callbacks are evaluated when commands are recorded:

- `ReflectionResolvedRadianceHistorySrv` resolves to the SRV for `readIndex`;
- `ReflectionResolvedRadianceCurrentSrv` resolves to the SRV for `readIndex ^ 1`;
- `ReflectionResolvedRadianceCurrentRtv` resolves to the RTV for `readIndex ^ 1`.
- auxiliary depth/normal history SRVs resolve to `readIndex`, and their current RTVs resolve to `readIndex ^ 1`.

The `ReflectionResolvedRadiance` debug view also reads the current physical slot selected by `readIndex ^ 1`. Selecting this view schedules `ReflectionEvaluatePass` and `TemporalReflectionPass` even when final reflection contribution is disabled, so evaluated and resolved signals can be inspected without LightPass weighting.

The selected indices remain constant throughout one frame. The future temporal pass reads the physical history slot only when history is valid and writes the physical current slot. `LightPass` reads that same current slot. The role exchange occurs after direct-queue submission and cannot happen while the frame graph is being executed. The next frame is submitted to the same queue, so queue ordering makes the promoted output available without a CPU-side GPU completion wait.

### Descriptor and RTV Inventory

Each physical slot owns one persistent SRV and one persistent RTV. The three history pairs therefore add exactly:

- six shader-visible SRV descriptors;
- six RTV descriptors;
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

The implemented minimum rejection consumes current/previous depth, current/previous visible world normal, and motion vectors. Future rejection may additionally consume visible roughness, reflection hit flag, hit distance, and hit normal. Adaptive thresholds, confidence/history length, and dedicated rejection debug views remain outside this contract phase.

## Reprojection and Rejection Contract

This section fixes the implementation boundary. Motion-vector reprojection plus bounds, depth, and normal rejection are wired; reflection-specific rejection is not.

### Motion Vector Convention

`GBuffer.MotionVector` is a render-resolution `DXGI_FORMAT_R16G16_FLOAT` texture produced by `GBufferPass`. Its shader value is:

```text
stored_motion_ndc
    = previous_ndc - current_ndc
    + motion_vector_jitter_cancellation_ndc
    + motion_vector_value_offset
```

`previous_ndc` includes both previous camera projection and `InstanceData.prevWorld`, so the source represents camera and object motion. The two added terms are Temporal Upscaler/debug policy, not geometric motion. Reflection reprojection must recover raw motion before converting NDC to texture UV:

```text
raw_motion_ndc
    = stored_motion_ndc
    - motion_vector_jitter_cancellation_ndc
    - motion_vector_value_offset

history_uv
    = current_uv + float2(0.5 * raw_motion_ndc.x,
                          -0.5 * raw_motion_ndc.y)
```

The Y sign differs because D3D NDC Y points up while texture UV Y points down. The first rejection is `historyValid && all(history_uv >= 0) && all(history_uv < 1)`. Out-of-bounds samples use current evaluated radiance with no history contribution.

Nearest sampling is sufficient for initial validity tests. Radiance sampling may become bilinear later, but bilinear radiance must never imply that depth/normal validity was also bilinearly accepted.

### Minimum Inputs and Auxiliary History

The first reprojection experiment consumes:

- current `ReflectionEvaluatedRadiance`;
- previous `ReflectionResolvedRadiance`;
- current `GBuffer.MotionVector`;
- the camera constants containing motion-vector cancellation/offset terms;
- current visible-surface depth and world-space normal;
- previous visible-surface depth and world-space normal aligned with the previous resolved-radiance slot.

Current depth and normal cannot validate a previous-frame sample by themselves. The auxiliary depth/normal history follows the same validity, invalidation, write index, post-submit role exchange, and resize lifecycle as `ReflectionResolvedRadiance`. It must not use swap-chain indices or Temporal Upscaler history ownership.

Do not pack depth, normal, confidence, or history length into `ReflectionResolvedRadiance.a`; resolved radiance remains opaque linear HDR. The implemented auxiliary representation is `R32_FLOAT` depth and `R16G16B16A16_FLOAT` world normal; both remain semantically distinct from radiance.

### Rejection Order

Apply rejection from cheapest and most general to more reflection-specific evidence:

1. reflection history valid;
2. reprojected UV inside the render extent;
3. previous visible-surface depth consistent with the current surface projected into the previous frame;
4. previous/current world-space visible normals consistent;
5. only if measured failures remain, compare visible roughness and reflection-specific hit flag, hit distance, or hit normal.

For static geometry, expected previous depth can be computed by reconstructing current world position and projecting it with the previous view-projection matrix. This test is approximate for moving geometry because the current GBuffer does not provide per-pixel previous world position or previous clip depth. The existing XY motion vector still reprojects moving objects, but depth rejection for those pixels may require a future previous-depth prediction signal. Do not silently treat current device depth as previous-view depth.

Normal rejection compares normalized world-space normals. The first policy accepts an absolute previous-device-depth difference of at most `0.002` and a normal dot product of at least `0.9`. These thresholds are policy, not resource meaning. Roughness should initially modulate accumulation weight only after core correspondence is valid; it is not a substitute for depth/normal rejection.

### First Implementation Slice

The first slice added motion-vector reprojection with bounds rejection while keeping the default history weight at zero. The second slice added depth/normal auxiliary history as MRT outputs and minimum rejection. A nonzero default is not allowed until the new camera-motion and disocclusion behavior has passed A/B validation. Spatial denoise, adaptive history length, neighborhood clamping, and reflection-hit rejection remain later work.

## Comparison Boundary

PathTracing comparison must name the signal boundary being compared. `ReflectionEvaluatedRadiance` is pre-temporal and pre-final-weighting. `ReflectionResolvedRadiance` is post-temporal but retains the same unweighted semantics. The final LightPass contribution is weighted and embedded in the lit scene, so it is a different comparison boundary.

DLSS Ray Reconstruction may motivate a different input contract later. No DLSS RR or Streamline SDK type or backend is connected by this document.
