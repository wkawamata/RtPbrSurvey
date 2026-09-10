# RenderGraph Resource Inspection

## Status

Draft specification for the RenderGraph resource inspection and preview workflow.

This document extends the completed read-only RenderGraph diagnostics roadmap. It does not make the runtime graph editable and does not expose renderer resources directly to application or host code.

## Goals

- Keep the existing Compact node presentation available.
- Add an Unreal-style presentation with stable input pins on the left and output pins on the right.
- Remove mouse-wheel zoom snapping and allow fine cursor-centered zoom control.
- Open inspectable Texture resources from RenderGraph selection, double-click, and context menus.
- Support multiple independent Debug Texture windows.
- Show fixed-size Texture and Buffer thumbnails without moving node pins.
- Show a small user-configurable index-color marker independently from the node background.
- Make close and distant geometry readable in every Depth visualization.
- Preserve RenderGraph resource-state and lifetime ownership.
- Reuse the same inspection path for DLSS SR and DLSS Ray Reconstruction inputs.

## Current Baseline

The current implementation provides:

- a read-only RenderGraph node editor with selection, search, filtering, details, lifetime, timing, validation, and barrier diagnostics;
- up to four `DebugTexturePreview.Output.N` textures;
- one independent `DebugTexturePreviewPass.N` conversion pass per live Inspector;
- one ImGui texture descriptor and stable GPU slot per live Inspector;
- open/focus, close, close-all, independent display settings, and bounded slot reuse;
- DLSS/RR Debug UI radio buttons and `Preview` buttons;
- a transitional `Preview LightPass` checkbox in the RenderGraph window.

`Preview LightPass` is not the final interaction. It should be replaced by resource-driven preview state.

## Existing RR Input Visualization

The following RR-related Texture resources already have full-screen debug views or standalone `Preview` entry points:

| Meaning | Resource | Current standalone preview |
|---|---|---|
| Noisy input radiance | `ReflectionEvaluatedRadiance` | Supported |
| Specular albedo | `ReflectionSpecularAlbedo` | Supported |
| Roughness | `ReflectionRoughness` | Supported |
| Specular hit distance | `ReflectionSpecularHitDistance` | Supported |
| Visible depth | `DepthStencil` | Supported |
| Motion vectors | `GBuffer.MotionVector` | Supported |
| Visible normal | `GBuffer.Normal` | Supported |
| Visible albedo | `GBuffer.Albedo` | Supported |

Current limitations:

- the preview cannot be opened from a RenderGraph resource node;
- `ReflectionResolvedRadiance` is available through existing render-view diagnostics but is not registered as a standalone Inspector source;
- `ReflectionRayHit`, ray color, material, and emission resources are diagnostics, not part of the current minimum native RR input contract;
- generic D3D12 Buffer inspection is not implemented.

## Node Presentation

### Style Selection

The RenderGraph toolbar provides two presentation modes:

- `Compact`: preserves the current node layout and labels.
- `UE-style`: uses a stable node width, input pins on the left, and output pins on the right.

Changing presentation mode must not change the `RenderGraphDocument`, resource identity, pass order, or runtime execution. Saved node positions are stored independently for each presentation mode so changing node dimensions does not damage the other layout.

### UE-style Pin Layout

- Pass Read pins are placed on the left edge.
- Pass Write pins are placed on the right edge.
- Resource Write input is placed on the left edge.
- Resource Read output is placed on the right edge.
- Input and output lists are independently stable-sorted.
- Pin rows reserve stable vertical positions for the current topology.
- Timing, lifetime, state, and validation text do not determine pin anchor positions.
- Thumbnail updates do not change node dimensions or pin positions.

### Zoom

- Mouse-wheel zoom is smooth and does not snap to predefined levels.
- Zoom remains centered on the mouse cursor.
- One wheel notch uses a small multiplicative step; the initial target is `1.05`.
- Existing minimum and maximum zoom limits remain in effect.
- `Fit Graph` and `Focus Selected` continue to work.

### Index Color

Every pass and resource node has an index-color marker that is independent from the node background color:

- The marker is a fixed-size square in the title row. The initial target is `10 x 10` pixels.
- The default color for ordinary nodes is neutral gray (`#808080`).
- `DLSS SR` and `DLSS Ray Reconstruction` pass nodes use NVIDIA green (`#76B900`) as their technology default.
- The marker does not replace the node title, resource type, validation icon, or selection state. Color must not be the only indication of node meaning.
- The context menu provides `Index Color`, a color editor, and `Reset Index Color`.
- A user override is stored by stable node identity and remains valid in both Compact and UE-style presentation modes.
- Reset restores the node's semantic default: NVIDIA green for DLSS nodes and gray for ordinary nodes.
- Index-color changes are local diagnostic preferences. They must not mutate `RenderGraphDocument`, pass execution, resource identity, or graph snapshots.

### Node Presentation Metadata

Node identity and display presentation remain separate. Runtime pass names continue to provide stable IDs, while an optional renderer-owned presentation descriptor supplies display-only metadata:

```cpp
struct RenderGraphNodePresentation
{
    std::string stableNodeId;
    std::string displayName;
    std::string technologyName;
    std::string versionText;
    uint32_t defaultIndexColor;
};
```

DLSS pass presentation is:

| Stable pass identity | Display name | Technology | Default index color | Version rows |
|---|---|---|---|---|
| `TemporalUpscalerPass` | `DLSS SR` | `NVIDIA DLSS` | `#76B900` | Streamline plugin and NGX versions when available |
| `DlssRayReconstructionPass` | `DLSS Ray Reconstruction` | `NVIDIA DLSS` | `#76B900` | Streamline plugin and NGX versions when available |

The explicit `DLSS` label must remain visible at normal zoom. Versions may collapse into the details pane or tooltip at low zoom. Version values reuse the existing Streamline diagnostics query; the graph must not issue a second SDK query per frame. If a version is unavailable, display `Version unavailable` rather than inventing a value.

## Resource Interaction

### Selection

Single-click selects a node and updates the existing details pane. Selection alone does not allocate a preview resource or add a render pass.

### Double-click

- Inspectable Texture resource: open or focus its Preview window.
- Inspectable Buffer resource: open or focus its Buffer Inspector when a compatible view is registered.
- Pass node: retain the current selection/focus behavior; do not guess an output resource.
- Unsupported resource: keep the node selected and show the unsupported reason in the details pane.

### Context Menu

Texture and Buffer resource nodes expose:

- `Preview`
- `Pin Preview`
- `Copy Resource Name`
- `Close Preview` when that resource is open
- `Index Color` and `Reset Index Color`

Unsupported actions are disabled and display a concise reason. Pass nodes may later expose a list of output resources, but that is outside the first implementation slice.

### RenderGraph Window Status

Remove the transitional `Preview LightPass` checkbox. Replace it with:

- active preview count;
- selected Preview resource name;
- `Close All` when at least one preview is open.

The DLSS/RR Debug UI radio buttons remain available for full-screen inspection. Its buttons use the concise label `Preview` and use the same preview manager as RenderGraph node actions.

The DLSS input and RR input radio-button groups each use a fixed three-items-per-row layout. The next item starts on a new row after every third item. Item width is stable so support/status changes do not reflow the grid.

## Multiple Preview Windows

Each live Inspector owns a stable preview slot:

```cpp
struct DebugTexturePreviewSlot
{
    uint64_t id;
    std::string sourceResourceName;
    DebugTexturePreviewSettings settings;
    std::string outputResourceName;
};
```

Required behavior:

- Opening an already-visible resource focuses its existing window.
- `Preview` opens a new independent window when the selected resource is not already visible; it does not replace another resource's window.
- `Pin Preview` creates or preserves an independent slot through the same manager. It is retained for RenderGraph context-menu workflows.
- Existing Preview windows remain visible when another resource is opened.
- Each window has independent semantic, channel, filter, exposure, scale, and offset controls.
- Each slot has its own RenderGraph preview pass, output Texture, RTV/SRV bindings, and ImGui descriptor.
- Closing a slot retires GPU resources and descriptors only after relevant GPU work completes.
- Closing the final slot removes all preview passes.
- The first implementation supports at most four live GPU preview slots and reports the limit in the UI.
- The first Preview window uses the current default Preview position and size.
- Unsaved windows use a two-column reverse-N order from the upper-right: upper-right, upper-left, lower-right,
  then lower-left.
- `Arrange` reapplies this layout to all live Preview windows on request.
- After first use, each Preview window behaves as a normal movable/resizable ImGui window. User-selected position and size are restored by stable window identity.
- Automatic default positioning uses `ImGuiCond_FirstUseEver` or equivalent behavior and must not overwrite a user-moved window every frame.

Every Preview window must display its own output. Multiple windows must never alias one shared output unless they intentionally reference the same preview slot.

## Resource Inspection Metadata

`RenderGraphDocument` remains a snapshot and must not own D3D12 pointers or descriptor handles. Renderer-owned inspection metadata is registered separately:

```cpp
enum class DebugResourceViewKind
{
    Texture,
    StructuredBuffer,
    RawBuffer,
    ScalarSeries,
    VectorSeries,
    Counter,
};

struct DebugResourceViewDescriptor
{
    std::string resourceName;
    DebugResourceViewKind viewKind;
    DebugTextureSemantic semantic;
    DXGI_FORMAT format;
    uint32_t elementCount;
    uint32_t elementStride;
};
```

The renderer keeps resource and descriptor resolvers behind this registry. The App, `SceneRenderer`, and `RenderGraphDocument` do not receive raw D3D12 resource ownership.

The details pane reports whether the selected resource is inspectable and why an unsupported resource cannot be opened.

## Node Thumbnails

### Texture

- Reserve a fixed `96 x 54` thumbnail area in UE-style resource nodes.
- Convert Color, Normal, Depth, MotionVector, and Scalar semantics through the existing preview shader path.
- Use nearest-neighbor sampling by default.
- Use a stable placeholder when the resource is unavailable or unsupported.

## Depth Visualization

Raw hardware depth is not a useful default diagnostic image for a perspective camera with a large far plane because most visible values cluster near `1.0` and appear white. Every view registered with `DebugTextureSemantic::Depth` uses the same depth-visualization transform.

This includes:

- the full-screen `Depth` render view;
- DLSS/RR Depth input Preview windows;
- RenderGraph Depth resource Preview windows;
- Depth node thumbnails;
- future history-depth inspectors.

### Conversion

1. Sample Depth with nearest-neighbor filtering.
2. Use current camera projection data to convert device depth to positive view-space distance.
3. Normalize it through the selected display range.
4. Apply optional inversion and gamma.
5. Produce grayscale display color before generic output transfer handling.

Perspective and orthographic projections must both be supported. The shader must use camera/projection constants and must not assume a fixed near/far plane. Clear depth remains visually identifiable and must not produce NaN or infinity.

### Controls

Each full Preview window exposes:

- `Depth Mode`: `Raw Device`, `Linear View`, or `Log View`;
- `Display Near` in view-space units;
- `Display Far` in view-space units;
- `Gamma`;
- `Invert`;
- `Reset Depth Tone`.

`Display Near` is always positive and less than `Display Far`. Controls use drag/slider behavior suitable for both sub-unit and large scene ranges. Existing generic exposure, scale, and offset controls remain available as advanced post-adjustments but are not the primary Depth mapping.

Default behavior:

- `Log View` is the default Depth mode so nearby geometry remains distinguishable with a large camera far plane.
- `Display Near` starts from the active camera near plane.
- `Display Far` starts from a bounded diagnostic distance derived from the camera range rather than blindly using a very large far plane.
- Near maps to black and Far/clear depth maps to white unless `Invert` is enabled.
- `Gamma` defaults to `1.0`.

Depth tone settings are independent per Preview window. The full-screen Depth render view owns one shared setting. Thumbnails use the registered resource default and do not add inline sliders to node geometry.

### Buffer

- Structured Buffer: compact element/count summary.
- Scalar series: histogram or heat strip.
- Vector series: RGB/direction heatmap when explicitly registered.
- Counter: occupancy or usage bar.
- Raw/unknown Buffer: size and format placeholder only.

### Update Policy

- Do not process every graph resource every frame.
- Update selected and open resources every frame.
- Throttle visible, unselected node thumbnails to a lower rate.
- Do not update filtered or off-screen thumbnails.
- Copy transient content immediately after its selected producer into persistent preview storage; do not extend every transient resource to the end of the frame.
- Use a thumbnail atlas or bounded slot pool to limit descriptor and memory growth.

## RenderGraph and Lifetime Rules

- A preview pass declares its source as a Read and its output as a Write.
- ImGui declares every displayed preview output as a `PIXEL_SHADER_RESOURCE` Read.
- State changes are performed through RenderGraph transitions, not by changing registry state without issuing a barrier.
- A dynamically selected transient source remains alive through its preview copy/conversion pass.
- ImGui retains a COM reference to resources referenced by already-built draw data across resize/recreation.
- Preview and thumbnail diagnostics must not change normal producer ownership or alter DLSS SR/RR results.

## Work Units

### RI-01 Interaction and Smooth Zoom

- Enable smooth wheel zoom with the agreed fine step.
- Add resource-node double-click action reporting.
- Add the resource-node context menu.
- Replace `Preview LightPass` with generic preview status.
- Keep the existing single preview slot for this slice.

### RI-02 Inspectable Resource Registry

- Add renderer-owned inspection descriptors and resolvers.
- Register current DLSS SR/RR input Textures.
- Register `ReflectionResolvedRadiance` as an RR comparison output.
- Show support status and unsupported reasons in the details pane.

### RI-03 Multiple Texture Preview Slots

- Add independent GPU outputs and ImGui descriptors.
- Implement independent open/focus, pinning, close, and close-all behavior.
- Add per-window display controls.
- Preserve user-selected window positions and sizes; cascade only windows without saved placement.
- Lay out DLSS and RR input radio buttons as three items per row and label the action button `Preview`.
- Validate resize, DLSS enable/disable, and RR enable/disable while windows are open.

### RI-04 Switchable Node Presentation

- Preserve Compact mode.
- Add UE-style left/right pin placement.
- Persist positions per style.
- Verify ping-pong role changes do not move pins or resize nodes.
- Add the fixed index-color square and persist user overrides by stable node identity.
- Add renderer-owned DLSS display metadata without renaming runtime passes.

### RI-05 Texture Thumbnails

- Add fixed node thumbnail regions.
- Add bounded thumbnail storage and update scheduling.
- Register the RR input Texture semantics.

### RI-06 Buffer Inspector and Thumbnails

- Add structured and raw Buffer details.
- Add explicitly registered histogram/heatmap modes.
- Avoid generic interpretation when stride or schema is unavailable.

### RI-07 Common Depth Visualization

- Add projection-aware device-depth linearization shared by full-screen and Preview paths.
- Add Raw Device, Linear View, and Log View modes.
- Add display Near/Far, Gamma, Invert, and Reset controls.
- Apply the common mapping to Depth thumbnails without changing node dimensions.
- Validate perspective, orthographic, clear-depth, resized, DLSS render-size, and RR input cases.

## Acceptance Criteria

- Compact mode remains visually and behaviorally compatible.
- UE-style mode keeps all input anchors on the left and output anchors on the right.
- Mouse-wheel zoom supports fine adjustment without level snapping.
- Double-click and context-menu `Preview` produce the same result.
- At least four Texture previews can remain visible simultaneously with distinct content.
- Opening a different RR input with `Preview` leaves existing Preview windows visible.
- The first window keeps the current default placement, while user-moved windows retain their positions and sizes.
- DLSS and RR input radio buttons wrap after every third item and the action button reads `Preview`.
- Full-screen, RR input, RenderGraph Preview, and thumbnail Depth views use the same projection-aware mapping.
- Default Log View Depth visibly separates nearby geometry instead of producing a nearly solid white image.
- Depth display Near/Far, Gamma, and Invert can be adjusted without changing the source resource.
- RR noisy radiance, specular albedo, roughness, hit distance, depth, motion vectors, normal, and albedo can be opened from RenderGraph resource nodes.
- RR native/fallback resolved output can be displayed beside its inputs.
- Node thumbnails never move pins when their content updates.
- Every node shows a fixed index-color marker without changing its background color.
- Ordinary nodes default to gray; DLSS SR/RR nodes default to NVIDIA green and are explicitly labeled `DLSS`.
- User index-color overrides survive presentation-mode changes and reset to the correct semantic default.
- DLSS node details show the available Streamline plugin and NGX versions without adding per-frame SDK queries.
- Unsupported Buffer nodes explain why visualization is unavailable.
- Enabling/disabling DLSS or resizing with previews open does not abort.
- Debug x64 build succeeds.
- D3D12 Debug Layer reports zero new errors.
- RenderGraph barrier diagnostics report zero mismatches introduced by inspection passes.

## Out of Scope

- Mutating the runtime RenderGraph from the inspection UI.
- Editing shader resources or Buffer values.
- Automatically interpreting arbitrary structured Buffer schemas.
- Vendor-specific types in RenderGraph, App, Scene, or public Runtime interfaces.
- Unbounded preview or thumbnail allocation.
