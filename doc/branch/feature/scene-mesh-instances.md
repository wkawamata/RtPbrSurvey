# Host Scene Mesh Instances

`Engine::SceneBuilder` can pack several meshes into one scene and select one mesh for each instance.
The public `SceneMeshId` is backend-neutral; renderer buffer ranges and acceleration structures remain internal.

## Host example

```cpp
Engine::SceneBuilder builder;

const uint32_t floorMaterial = builder.AddSolidColorMaterial(90, 100, 110, 255);
const uint32_t bodyMaterial = builder.AddSolidColorMaterial(70, 150, 220, 255);
const uint32_t wheelMaterial = builder.AddSolidColorMaterial(220, 120, 55, 255);

const Engine::SceneMeshId cube = builder.AddCube(1.0f);
const Engine::SceneMeshId wheel = builder.AddCylinder(
    0.5f,
    1.0f,
    16,
    Engine::CylinderCapMode::Both);

builder.AddInstance(
    cube,
    DirectX::XMMatrixScaling(200.0f, 0.1f, 200.0f),
    floorMaterial);
builder.AddInstance(
    cube,
    DirectX::XMMatrixTranslation(0.0f, 1.0f, 0.0f),
    bodyMaterial);
builder.AddInstance(
    wheel,
    DirectX::XMMatrixScaling(0.7f, 1.2f, 0.4f) *
        DirectX::XMMatrixTranslation(1.5f, 0.5f, 0.0f),
    wheelMaterial);

renderer.SetScene(builder.GetScene());
renderer.ReloadSceneResources(builder.GetScene());
```

The ordinary DirectXMath world-matrix convention is unchanged. The overload accepting `prevWorld` is available
for moving objects.

## Cylinder convention

- The cylinder is centered at the local origin.
- Its axis is local Y.
- `radius` controls the local XZ radius.
- `height` spans `[-height / 2, +height / 2]` on local Y.
- `radialSegments` must be at least 3.
- `CylinderCapMode::Both` adds the positive-Y and negative-Y caps.
- `CylinderCapMode::None` leaves both ends open.
- Side normals are flat per radial segment.
- Side and cap UV coordinates are generated.
- Non-uniform world scaling uses the renderer's inverse-transpose normal path.

## Loaded meshes

`AddGltfMesh(path)` appends a glTF mesh and returns `std::optional<SceneMeshId>`. Texture indices, material
indices, and per-vertex material references are remapped into the scene-global packed storage. The existing
`LoadGltfMesh(path)` replacement-style API remains available for compatibility.

## Compatibility

The existing `AppendCube()`, `AppendSphere()`, and `AddInstance(world, materialId)` APIs retain their default
composite-mesh behavior. A legacy composite cannot be extended after independent mesh ranges have been added;
new host code should consistently use `AddCube()`, `AddSphere()`, `AddCylinder()`, and the mesh-ID instance
overloads.

Scenes that do not provide explicit mesh ranges are treated as one full-buffer mesh with ID 0.

## Renderer behavior

- Raster passes issue one draw per visible instance in this first increment. `StartInstanceLocation` preserves
  the scene instance index used by shaders.
- Ray tracing builds one BLAS per mesh range. Each TLAS instance references the BLAS selected by its mesh ID.
- Hybrid Reflection resolves `CommittedPrimitiveIndex()` through the selected packed mesh range.
- Draw batching by contiguous mesh ID is a possible follow-up optimization and does not change the public API.

The host-facing CMake target remains `RtPbrSurvey::SceneRenderer`.
