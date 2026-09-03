# Host Debug Lines

## Contract

`Runtime/SceneRenderer.h` exposes backend-neutral persistent debug lines:

- `DebugLineHandle` is an opaque `uint32_t`; zero is invalid.
- `DebugLineDesc` contains world-space start/end positions, linear RGBA color, visibility, and `DebugLineDepthMode`.
- `AddDebugLine`, `UpdateDebugLine`, `RemoveDebugLine`, and `ClearDebugLines` are frame/render-thread operations.
- Handles encode slot index and generation. Removed or cleared handles become stale and are safe no-ops.
- The host registry holds at most 1024 lines. Capacity exhaustion returns the invalid handle.

The caller must serialize API calls with `SceneRenderer::RunFrame`; no cross-thread synchronization is provided.

## Rendering

Visible host lines are assembled each frame into depth-tested and overlay vertex groups. Existing specular diagnostic lines are appended to the overlay group. The debug-line render pass reads scene depth as `DEPTH_READ`, binds the scene DSV, and uses separate depth-tested and depth-disabled pipeline states.

Colors are linear RGBA values. Alpha is retained in the vertex contract, but translucent blending is not implemented. Thick lines, dashed lines, and line antialiasing are also outside this API's scope.

## Validation

- `Tests/DebugLineTests.cpp` covers add, update, hide, remove, stale and invalid handles, clear, capacity, CPU vertex assembly, and depth-tested/overlay classification.
- `Tests/DebugLineHostCompileProbe.cpp` verifies the public `SceneRenderer` API shape.
- `Tests/HostDebugLineCompile` is an external CMake `add_subdirectory` host fixture.
