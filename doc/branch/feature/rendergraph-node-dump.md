# RenderGraph Node Dump Investigation

## Purpose

This branch defines the smallest useful first step toward RenderGraph diagnostics and visualization. The immediate goal is to make the current pass/resource relationships inspectable without changing RenderGraph execution, descriptor management, PathTracing, or temporal-upscaler integrations.

The current frame-graph model already exposes the data needed for a diagnostic view:

- `Engine::RenderPassGraph` stores an ordered list of passes.
- Each `Engine::RenderPass` exposes named read and write resource usages with `D3D12_RESOURCE_STATES`.
- `AnalyzeResourceLifetimes()` derives first/last pass indices.
- `RenderTextureSpec` and the transient-resource registry distinguish persistent resources from transient resources.

This makes a library-free diagnostic representation practical before committing to an interactive node-editor dependency.

## Candidate Comparison

| Candidate | License | Integration and dependencies | Maintenance signal (checked 2026-07-24) | Build impact | Fit for the first version |
|---|---|---|---|---|---|
| [`imgui-node-editor` / `ax::NodeEditor`](https://github.com/thedmd/imgui-node-editor) | [MIT](https://github.com/thedmd/imgui-node-editor/blob/master/LICENSE) | Requires Dear ImGui and C++14. Upstream describes copy-pasting a group of root-level sources. A practical integration includes the editor implementation/API, canvas and math helpers, and its small JSON support, so it is materially more than a single translation unit. It renders through ImGui and therefore does not require a separate DX12 backend, but compatibility must be verified against the exact ImGui revision used by this repository. | Latest tagged release shown by GitHub is `v0.9.3` (2023-10-14). The repository has newer compatibility PR activity, including ImGui 1.9x fixes, but unreleased fixes indicate that pinning a known-good commit and testing upgrades would be important. | Add vendored sources and license notice, project compile/include entries, an editor context lifecycle, stable node/pin IDs, optional layout-state persistence, and warning/ImGui compatibility checks. | Strongest choice if a polished, navigable graph canvas becomes a confirmed requirement; too much surface area for the initial read-only diagnostic. |
| [`imnodes`](https://github.com/Nelarius/imnodes) | [MIT](https://github.com/Nelarius/imnodes/blob/master/LICENSE.md) | Upstream describes it as dependency-free beyond Dear ImGui and distributes it as three files: `imnodes.cpp`, `imnodes.h`, and `imnodes_internal.h`. It owns no DX12 backend and uses the existing ImGui renderer. Application code owns graph state, IDs, and presentation. | Latest tagged release shown by GitHub is `0.5` (2022-03-09). The small API and source footprint reduce adoption cost, but the old release date raises compatibility/maintenance risk that must be tested against the repository's ImGui revision. | Add three vendored files and license notice, one compile entry, include access, context lifecycle, stable IDs, and compatibility tests. Lower integration cost than `imgui-node-editor`. | Best external-library candidate if a lightweight interactive viewer is approved later, but still unnecessary for the first diagnostic dump. |
| No external library: ImGui table/text tree and DOT text export | Repository code only. Emitting DOT text does not link or vendor Graphviz. Graphviz itself is needed only as an optional external viewer if a developer wants rendered output. | Uses the current ImGui integration for an in-app view and/or writes plain DOT syntax. No new runtime, package, submodule, backend, or third-party source is required. A `.dot` file can be inspected as text even when Graphviz is not installed. | Maintained entirely with the RenderGraph data model. DOT is a stable documented graph language; the exporter only needs a small escaping routine and deterministic IDs/order. | A small diagnostic helper plus an optional Debug UI entry. No project-wide dependency configuration. Tests can validate exact text or semantic fragments. | Recommended first version. It validates the diagnostic schema and workflow before selecting a canvas library. |

## License and Repository-Ingestion Notes

Both node-editor candidates use the MIT License and permit redistribution when the copyright and permission notice are retained. If either is adopted, its source must be pinned to a tag or commit, placed in a clearly named third-party directory, accompanied by its upstream license and provenance, and explicitly added to `RtPbrSurvey.vcxproj`. Vendoring, adding a submodule, or adding a download/package script requires explicit user approval and is not part of this investigation.

The recommended DOT exporter only emits text conforming to the [DOT language](https://graphviz.org/doc/info/lang.html). It should not invoke, bundle, or link Graphviz. Developers may optionally use Graphviz's `dot` tool externally to render the file; that optional tool is separate from the application and its build.

## Recommendation

Start without an external node-editor library.

First define one read-only diagnostic snapshot and two presentations over it:

1. A deterministic text dump suitable for logs, tests, and issue reports.
2. A deterministic DOT export suitable for offline graph visualization.
3. Optionally, a compact ImGui table/tree that displays the same snapshot without editing it.

The snapshot should be independent of ImGui and DOT formatting. This separation avoids coupling RenderGraph data collection to a UI library and gives a future node editor a stable input model. It also reveals whether the useful unit is pass-to-resource, pass-to-pass dependency, lifetime, state transition, or some combination before a canvas interaction model is selected.

If interactive navigation is later justified, evaluate `imnodes` first because its three-file distribution and caller-owned state are a good match for a read-only viewer. Choose `imgui-node-editor` instead only if richer navigation, selection, grouping, persistent layouts, and extensible pin/node presentation outweigh its larger integration and compatibility surface.

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

1. Freeze or version the diagnostic snapshot schema.
2. Record usability needs: graph size, filtering, grouping, selection synchronization, search, state-transition visibility, and layout persistence.
3. Prototype the same snapshot with `imnodes` and, only if needed, `imgui-node-editor` outside the production integration.
4. Request explicit approval for the selected dependency and ingestion method.
5. Pin an upstream tag/commit, retain its MIT license, document provenance, and add a focused compatibility/build test.
6. Keep the view read-only; editing the RenderGraph should be a separate design effort.

## Build Integration Assessment

The application already owns Dear ImGui initialization and its Win32/DX12 backend in `Ui/ImGuiSystem`. Either node-editor candidate would sit above that existing ImGui context and should not add a graphics backend. It would still require lifecycle hooks for its own context and explicit `.vcxproj` source entries.

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
