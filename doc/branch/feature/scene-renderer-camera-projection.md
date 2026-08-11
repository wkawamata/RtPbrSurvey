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

## Matching 2D And 3D Framing

The public projection framing utilities convert in both directions:

```cpp
const float focusDistance = 100.0f;
camera.fov = Engine::PerspectiveFovYFromOrthographicHeight(camera.orthographicHeight, focusDistance);
camera.orthographicHeight = Engine::OrthographicHeightFromPerspectiveFovY(camera.fov, focusDistance);
camera.projection = Engine::CameraProjection::Perspective;
```

FOV Y is expressed in degrees. `orthographicHeight` and `focusDistance` must use the same world unit. The contract preserves vertical framing and therefore does not depend on aspect ratio.

The relationships are:

```text
fovY = 2 * atan(orthographicHeight / (2 * focusDistance))
orthographicHeight = 2 * focusDistance * tan(fovY / 2)
```

The strict utilities return quiet NaN when an input is non-finite or non-positive. FOV Y must also be strictly between 0 and 180 degrees. A result that cannot be represented as a positive finite `float` also returns quiet NaN.

`MatchPerspectiveToOrthographic()` remains available for source and behavior compatibility. It preserves its original input clamps and its 0.1-to-179-degree output clamp; new host code should use the explicitly named strict utilities.

For a smooth 2D-to-3D transition, keep `gazePoint`, `up`, and the focus plane fixed. Move the Perspective camera backward while reducing FOV with this helper. The focus plane keeps the same framing; objects at other depths gradually gain perspective parallax. At a sufficiently long focus distance, the Perspective result becomes visually indistinguishable from Orthographic and the projection mode can switch without a visible scale jump.

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
