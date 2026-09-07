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
- Preserve RenderGraph resource-state and lifetime ownership.
- Reuse the same inspection path for DLSS SR and DLSS Ray Reconstruction inputs.

## Current Baseline

The current implementation provides:

- a read-only RenderGraph node editor with selection, search, filtering, details, lifetime, timing, validation, and barrier diagnostics;
- one persistent `DebugTexturePreview.Output` texture;
- one `DebugTexturePreviewPass` that converts a selected Texture into displayable RGBA16F;
- one ImGui texture descriptor and one effective GPU preview slot;
- multiple-inspector data structures, although the UI currently closes the previous inspector when another preview is opened;
- DLSS/RR Debug UI radio buttons and `Open Preview` buttons;
- a transitional `Preview LightPass` checkbox in the RenderGraph window.

`Preview LightPass` is not the final interaction. It should be replaced by resource-driven preview state.

## Existing RR Input Visualization

The following RR-related Texture resources already have full-screen debug views or standalone `Open Preview` entry points:

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

- only one resource can be displayed in the standalone preview at a time;
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

## Resource Interaction

### Selection

Single-click selects a node and updates the existing details pane. Selection alone does not allocate a preview resource or add a render pass.

### Double-click

- Inspectable Texture resource: open or focus its quick Preview window.
- Inspectable Buffer resource: open or focus its Buffer Inspector when a compatible view is registered.
- Pass node: retain the current selection/focus behavior; do not guess an output resource.
- Unsupported resource: keep the node selected and show the unsupported reason in the details pane.

### Context Menu

Texture and Buffer resource nodes expose:

- `Open Preview`
- `Pin Preview`
- `Copy Resource Name`
- `Close Preview` when that resource is open

Unsupported actions are disabled and display a concise reason. Pass nodes may later expose a list of output resources, but that is outside the first implementation slice.

### RenderGraph Window Status

Remove the transitional `Preview LightPass` checkbox. Replace it with:

- active preview count;
- selected/quick-preview resource name;
- `Close All` when at least one preview is open.

The DLSS/RR Debug UI radio buttons remain available for full-screen inspection. Its `Open Preview` actions use the same preview manager as RenderGraph node actions.

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
- `Open Preview` uses the quick unpinned slot when possible.
- `Pin Preview` promotes the quick slot or creates an independent slot.
- Pinned windows remain visible when another quick preview is opened.
- Each window has independent semantic, channel, filter, exposure, scale, and offset controls.
- Each slot has its own RenderGraph preview pass, output Texture, RTV/SRV bindings, and ImGui descriptor.
- Closing a slot retires GPU resources and descriptors only after relevant GPU work completes.
- Closing the final slot removes all preview passes.
- The first implementation supports at most four live GPU preview slots and reports the limit in the UI.

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
- Implement quick-preview reuse, pinning, close, and close-all.
- Add per-window display controls.
- Validate resize, DLSS enable/disable, and RR enable/disable while windows are open.

### RI-04 Switchable Node Presentation

- Preserve Compact mode.
- Add UE-style left/right pin placement.
- Persist positions per style.
- Verify ping-pong role changes do not move pins or resize nodes.

### RI-05 Texture Thumbnails

- Add fixed node thumbnail regions.
- Add bounded thumbnail storage and update scheduling.
- Register the RR input Texture semantics.

### RI-06 Buffer Inspector and Thumbnails

- Add structured and raw Buffer details.
- Add explicitly registered histogram/heatmap modes.
- Avoid generic interpretation when stride or schema is unavailable.

## Acceptance Criteria

- Compact mode remains visually and behaviorally compatible.
- UE-style mode keeps all input anchors on the left and output anchors on the right.
- Mouse-wheel zoom supports fine adjustment without level snapping.
- Double-click and context-menu Open Preview produce the same result.
- At least four pinned Texture previews can remain visible with distinct content.
- RR noisy radiance, specular albedo, roughness, hit distance, depth, motion vectors, normal, and albedo can be opened from RenderGraph resource nodes.
- RR native/fallback resolved output can be displayed beside its inputs.
- Node thumbnails never move pins when their content updates.
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
