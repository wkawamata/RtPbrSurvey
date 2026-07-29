# SceneRenderer Camera Projection

## Public Contract

`Engine::CameraState` supports both projection modes:

```cpp
Engine::CameraState camera;
camera.projection = Engine::CameraProjection::Perspective;
camera.fov = 60.0f;

camera.projection = Engine::CameraProjection::Orthographic;
camera.orthographicHeight = 10.0f;
```

`CameraState::fov` remains source-compatible with existing scenes and represents the vertical field of view in degrees. `orthographicHeight` is the visible world-space height; the renderer derives width from the current render aspect ratio.

`CameraState::up` defines the host-facing camera roll/orientation reference and defaults to world up:

```cpp
camera.pos = {0.0f, 20.0f, 0.0f};
camera.gazePoint = {0.0f, 0.0f, 0.0f};
camera.up = {0.0f, 0.0f, 1.0f};
camera.projection = Engine::CameraProjection::Orthographic;
```

This produces an exact top-down view without introducing a horizontal camera offset. The renderer normalizes and orthogonalizes the requested basis. Zero-length or parallel forward/up inputs use a stable fallback instead of producing a non-finite view matrix.

External hosts can update only the camera without copying or rebuilding the complete scene:

```cpp
renderer.SetCamera(camera);
const Engine::CameraState& activeCamera = renderer.GetCamera();
```

Scene geometry and GPU resources do not need to be reloaded after a camera change.

## Renderer Behavior

`CreateCameraProjectionMatrix()` is the shared projection boundary used by the regular camera constant buffer and Streamline frame constants.

- Perspective uses `XMMatrixPerspectiveFovLH`.
- Orthographic uses `XMMatrixOrthographicLH`.
- Both paths use the same view-projection and inverse view-projection flow, so world-position reconstruction, motion vectors, RayQuery passes, and render views consume the selected projection consistently.
- The regular view matrix and Streamline camera up/right/forward constants use the same resolved host-provided basis.
- Temporal jitter updates the perspective depth row or the orthographic translation row as appropriate.
- Projection parameters, render aspect, and camera up changes reset temporal history.
- Streamline receives its orthographic projection flag instead of assuming perspective.

## Ownership

Camera state remains scene/application state and is not part of `SceneRendererSettings`. A host such as TankPhysicsSandbox owns its Camera window, input mapping, orbit behavior, and camera persistence policy.

The standalone RtPbrSurvey app exposes Up, Projection, FOV Y, and Ortho Height controls in its existing Camera debug section. Scene config JSON persists up, projection, and orthographic height while treating missing fields as the legacy world-up Perspective defaults.
