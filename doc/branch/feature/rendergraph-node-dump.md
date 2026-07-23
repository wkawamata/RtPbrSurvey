# RenderGraph Node Dump Investigation

## Purpose

This branch defines the smallest useful first step toward RenderGraph diagnostics and visualization. The immediate goal is to make the current pass/resource relationships inspectable without changing RenderGraph execution, descriptor management, PathTracing, or temporal-upscaler integrations.

The current frame-graph model already exposes the data needed for a diagnostic view:

- `Engine::RenderPassGraph` stores an ordered list of passes.
- Each `Engine::RenderPass` exposes named read and write resource usages with `D3D12_RESOURCE_STATES`.
- `AnalyzeResourceLifetimes()` derives first/last pass indices.
- `RenderTextureSpec` and the transient-resource registry distinguish persistent resources from transient resources.

This makes a library-free diagnostic representation practical as the first implementation. The interactive UI library decision is already made: the later viewer/editor will use `imgui-node-editor` / `ax::NodeEditor`.

## Candidate Comparison

| Candidate | License | Integration and dependencies | Maintenance signal (checked 2026-07-24) | Build impact | Fit for the first version |
|---|---|---|---|---|---|
| [`imgui-node-editor` / `ax::NodeEditor`](https://github.com/thedmd/imgui-node-editor) | [MIT](https://github.com/thedmd/imgui-node-editor/blob/master/LICENSE) | Requires Dear ImGui and C++14. Upstream describes copy-pasting a group of root-level sources. A practical integration includes the editor implementation/API, canvas and math helpers, and its small JSON support, so it is materially more than a single translation unit. It renders through ImGui and therefore does not require a separate DX12 backend, but compatibility must be verified against the exact ImGui revision used by this repository. | Latest tagged release shown by GitHub is `v0.9.3` (2023-10-14). The repository has newer compatibility PR activity, including ImGui 1.9x fixes, but unreleased fixes indicate that pinning a known-good commit and testing upgrades would be important. | Add vendored sources and license notice, project compile/include entries, an editor context lifecycle, stable node/pin IDs, optional layout-state persistence, and warning/ImGui compatibility checks. | Selected for the future viewer/editor because its richer navigation, selection, grouping, extensible node/pin presentation, and persisted layout support match the intended path from diagnostics to editing. It is deliberately not required by the first dump implementation. |
| [`imnodes`](https://github.com/Nelarius/imnodes) | [MIT](https://github.com/Nelarius/imnodes/blob/master/LICENSE.md) | Upstream describes it as dependency-free beyond Dear ImGui and distributes it as three files: `imnodes.cpp`, `imnodes.h`, and `imnodes_internal.h`. It owns no DX12 backend and uses the existing ImGui renderer. Application code owns graph state, IDs, and presentation. | Latest tagged release shown by GitHub is `0.5` (2022-03-09). The small API and source footprint reduce adoption cost, but the old release date raises compatibility/maintenance risk that must be tested against the repository's ImGui revision. | Add three vendored files and license notice, one compile entry, include access, context lifecycle, stable IDs, and compatibility tests. Lower integration cost than `imgui-node-editor`. | Not selected. Its smaller footprint is attractive, but choosing it for the first viewer would create a likely library migration when editing and richer navigation are added. |
| No external library: ImGui table/text tree and DOT text export | Repository code only. Emitting DOT text does not link or vendor Graphviz. Graphviz itself is needed only as an optional external viewer if a developer wants rendered output. | Uses the current ImGui integration for an in-app view and/or writes plain DOT syntax. No new runtime, package, submodule, backend, or third-party source is required. A `.dot` file can be inspected as text even when Graphviz is not installed. | Maintained entirely with the RenderGraph data model. DOT is a stable documented graph language; the exporter only needs a small escaping routine and deterministic IDs/order. | A small diagnostic helper plus an optional Debug UI entry. No project-wide dependency configuration. Tests can validate exact text or semantic fragments. | Recommended first version. It validates the diagnostic schema and workflow before selecting a canvas library. |

## License and Repository-Ingestion Notes

Both node-editor candidates use the MIT License and permit redistribution when the copyright and permission notice are retained. If either is adopted, its source must be pinned to a tag or commit, placed in a clearly named third-party directory, accompanied by its upstream license and provenance, and explicitly added to `RtPbrSurvey.vcxproj`. Vendoring, adding a submodule, or adding a download/package script requires explicit user approval and is not part of this investigation.

The recommended DOT exporter only emits text conforming to the [DOT language](https://graphviz.org/doc/info/lang.html). It should not invoke, bundle, or link Graphviz. Developers may optionally use Graphviz's `dot` tool externally to render the file; that optional tool is separate from the application and its build.

## Decision and Recommendation

Use `imgui-node-editor` for the eventual interactive viewer/editor, but start implementation without integrating the library. This avoids disposable UI work while allowing the first milestone to remain a small, testable dump.

First define one read-only diagnostic snapshot and two presentations over it:

1. A deterministic text dump suitable for logs, tests, and issue reports.
2. A deterministic DOT export suitable for offline graph visualization.
3. Optionally, a compact ImGui table/tree that displays the same snapshot without editing it.

The snapshot should be independent of ImGui, `imgui-node-editor`, and DOT formatting. This separation avoids coupling graph semantics to a UI library and gives dump, read-only node view, and future editor a stable shared model.

## Model Architecture

Keep node information separate from the executable RenderGraph, but do not maintain it as an unrelated second source of truth.

Introduce a repository-owned intermediate model, tentatively named `RenderGraphDocument`, with three layers:

1. `RenderPassGraph` remains the authoritative runtime graph for execution.
2. A builder projects the runtime graph and registries into `RenderGraphDocument`.
3. Dump exporters and the `imgui-node-editor` adapter consume the document.

The document should use repository-owned IDs and types:

- `DocumentNodeId`, `DocumentPinId`, and `DocumentLinkId` are stable logical IDs.
- Pass nodes and resource nodes carry semantic data, not `ax::NodeEditor::NodeId`.
- Pins describe direction, access, resource state, and connection policy.
- Links describe pass/resource relationships and later editable connections.
- View-only state such as position, zoom, selection, and collapsed groups lives in a separate `RenderGraphEditorViewState`.

The `imgui-node-editor` layer maps document IDs to `ax::NodeEditor` IDs. No `ax::NodeEditor` type should appear in RenderGraph runtime headers or the document model. This adapter boundary allows the library version or integration details to change without rewriting graph semantics, while the decision to use `imgui-node-editor` remains stable.

### Read and edit flow

The first version is one-way:

`RenderPassGraph -> RenderGraphDocument -> text/DOT dump`

The read-only node view later adds:

`RenderGraphDocument + RenderGraphEditorViewState -> imgui-node-editor`

Editing should not mutate the runtime graph directly from UI callbacks. A future editor should emit explicit repository-owned commands such as add/remove pass, connect/disconnect resource, or update pass property. Commands are validated and applied to an authoring model, which then rebuilds and validates the executable `RenderPassGraph`. Failed validation leaves the last valid runtime graph intact.

This command boundary is important because the present `RenderPassGraph` is built procedurally and contains execution-facing objects. Treating its vectors as an editor database would couple UI gestures to runtime safety and make undo/redo, validation, serialization, and error reporting difficult.

### Ownership rule

- Runtime semantics: `RenderPassGraph` and its registries.
- Diagnostic/editor semantics: `RenderGraphDocument`.
- Canvas state: `RenderGraphEditorViewState`.
- Library-specific context, IDs, drawing, and callbacks: `ImguiNodeEditorRenderGraphView`.
- Future mutations: repository-owned editor commands and authoring model, never direct library callbacks into runtime containers.

## First Implementation Scope

The first code change should remain library-free and read-only.

### Diagnostic snapshot

Capture the graph after `BuildRenderPasses()` and validation, using stable diagnostic records rather than exposing mutable runtime objects:

- Pass: ordered index, display name, active/included status, pipeline key/name when resolvable, and operation key/name when resolvable.
- Resource: name, first/last pass indices, transient or persistent classification when registered, and known initial/current state when available.
- Edge: pass index, resource name, access type (`read` or `write`), and requested `D3D12_RESOURCE_STATES`.

The current graph only contains passes selected during `BuildRenderPasses()`. Therefore, in the first version, `active` means present in the built graph. Showing disabled candidate passes would require a separate authoring registry and is outside this scope.

### Text dump

Emit a deterministic pass-ordered report. Each pass lists reads and writes; each resource line includes the requested state and, when known, lifetime and transient/persistent classification. Use symbolic state names where practical and retain a hexadecimal fallback for combined or unknown flags.

### DOT export

Emit a directed bipartite graph:

- Pass nodes use one shape/color.
- Resource nodes use another shape/color.
- `resource -> pass` represents a read.
- `pass -> resource` represents a write.
- Edge labels include access and requested state.
- Resource labels include lifetime and transient/persistent classification when known.

The exporter must quote/escape identifiers and labels, use generated stable IDs rather than names as DOT IDs, and keep output deterministic for reviewable diffs.

### Debug UI entry

If an in-app entry is added, keep it under the existing Debug UI as a small read-only window or collapsing section. It may show a filter and copy/export controls, but it must not allow graph mutation, pass reordering, resource editing, or node-link creation.

### Explicitly deferred

- Interactive graph editing.
- Automatic graph layout inside the application.
- Persistent user-authored node positions.
- RenderGraph scheduling or lifetime-policy changes.
- Descriptor-heap changes.
- Any PathTracing, Streamline, DLSS, reflection-contract, or shadow-validation work.

## Future Node-Editor Path

After the dump format has been used on real graphs:

1. Implement and test `RenderGraphDocument` through deterministic text/DOT dumps.
2. Record usability needs: graph size, filtering, grouping, selection synchronization, search, state-transition visibility, and layout persistence.
3. Request explicit approval for the `imgui-node-editor` ingestion method.
4. Pin a known-good upstream tag/commit, retain its MIT license, document provenance, and add focused compatibility/build tests.
5. Add an `imgui-node-editor` adapter and read-only view over the existing document; do not fork the document schema for UI convenience.
6. Add repository-owned edit commands, validation, undo/redo, and serialization before enabling mutation.
7. Rebuild the executable graph from validated authoring data instead of editing the live runtime graph in place.

## Build Integration Assessment

The application already owns Dear ImGui initialization and its Win32/DX12 backend in `Ui/ImGuiSystem`. `imgui-node-editor` will sit above that existing ImGui context and should not add a graphics backend. It will still require lifecycle hooks for its own context and explicit `.vcxproj` source entries.

The no-library option adds only repository-owned C++ files if implemented. The project currently lists sources explicitly in `RtPbrSurvey.vcxproj`, so any future helper implementation must be added there. A documentation-only investigation changes no compile inputs.

## Validation

Build skipped: this step changes documentation only and introduces no source, project, dependency, or runtime change.

## Sources

- `imgui-node-editor` repository, dependencies, distribution guidance, features, and release metadata: <https://github.com/thedmd/imgui-node-editor>
- `imgui-node-editor` license: <https://github.com/thedmd/imgui-node-editor/blob/master/LICENSE>
- `imnodes` repository, three-file distribution, API overview, and release metadata: <https://github.com/Nelarius/imnodes>
- `imnodes` license: <https://github.com/Nelarius/imnodes/blob/master/LICENSE.md>
- Graphviz DOT language specification: <https://graphviz.org/doc/info/lang.html>
- Graphviz `dot` layout documentation: <https://graphviz.org/docs/layouts/dot/>
