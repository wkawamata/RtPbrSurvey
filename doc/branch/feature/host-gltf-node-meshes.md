# Host glTF Node Meshes

## Public Contract

`LoadGltfSceneAsset(path)` loads and owns one CPU-side glTF model without exposing tinygltf types. `GetGltfMeshNodeNames(asset)` enumerates named mesh nodes reachable from the default scene in traversal order. Duplicate names are retained in the list so hosts can diagnose authored assets.

`SceneBuilder::AddGltfNodeMesh(asset, nodeName)` returns `GltfNodeMeshAddResult`:

- `Success`: `meshId` contains a new independent `SceneMeshId`.
- `InvalidAsset`: the CPU asset is empty or failed to load.
- `NodeNotFound`: no reachable mesh node has the requested name.
- `DuplicateNodeName`: more than one reachable mesh node has the requested name.
- `MeshConversionFailed`: the selected node exists but its primitives cannot be converted.

The existing `SceneBuilder::AddGltfMesh(path)` contract remains source compatible and continues to flatten the complete default scene into one mesh range.

## Extraction Semantics

- Only the selected node's own mesh is extracted. Child node meshes are not included implicitly.
- The selected node's local transform and all ancestor transforms are baked into positions, normals, and tangents using the existing glTF-to-renderer left-handed conversion.
- Materials and decoded textures are deep-copied into `SceneBuilder`.
- Material and texture indices are remapped to the builder-global arrays exactly as they are for `AddGltfMesh(path)`.
- Missing and duplicate node names never select a first match and do not mutate the builder.

## Lifetime

`GltfSceneAsset` owns the parsed CPU model and may be shared or copied cheaply. It must remain alive while querying names or adding nodes.

After `AddGltfNodeMesh` succeeds, all vertices, indices, materials, and texture pixels needed by `SceneBuilder` have been copied. The host may release `GltfSceneAsset` immediately. A later node addition requires retaining or reloading the asset.

## Validation

`Tests/Fixtures/Gltf/multi-node.gltf` covers:

- multiple independently selected mesh nodes;
- ancestor and selected-node transforms;
- left-handed Z conversion;
- child mesh exclusion;
- material and texture global remapping;
- missing, duplicate, and invalid asset statuses;
- releasing the CPU asset after additions.

Verification on 2026-08-13:

- MSBuild Debug x64 succeeded.
- CMake SDK-free Debug build succeeded.
- CTest passed 10/10, including the multi-node fixture.
- A separately managed GLB was loaded once, enumerated three named mesh nodes, and added each as an independent `SceneMeshId` through the generic inspection mode.
