# SceneRenderer Settings

## Purpose

`SceneRendererDebugUi` changes renderer-owned state such as direct-light direction, shadow bias, tone mapping, and hybrid reflection settings. External hosts need to save and restore those values without constructing `RtPbrSurveyApp` or depending on application-private scene configuration code.

The standalone RtPbrSurvey application and external hosts such as TankPhysicsSandbox should use the same settings model and JSON schema for renderer-owned state.

## Ownership

Runtime owns:

- `RtPbrSurvey::SceneRendererSettings`
- capture from and apply to `SceneRenderer`
- versioned JSON serialization and deserialization
- validation and backward-compatible defaults for missing fields

The host owns:

- the settings file path and filename
- when settings are loaded or saved
- UI commands such as Save, Load, and Reset
- application-specific settings that contain or sit beside renderer settings

Runtime does not perform file I/O. This keeps `SceneRenderer` usable by applications with different configuration directories, profile systems, and persistence policies.

## Settings Boundary

`SceneRendererSettings` contains renderer-owned, persistent controls:

- PBR lighting
- RayQuery shadows
- temporal upscaler settings
- hybrid reflections
- tone mapping
- rendering path and render view mode
- back-buffer clear color
- LightPass debug gradient
- specular debug-line settings

It intentionally excludes:

- frame timing and capability status
- pixel-pick results and one-shot dump requests
- scene resources and display instance count
- camera state
- environment resources that require an explicit GPU reload
- RtPbrSurvey scene selection and Tank physics state

## Public API

```cpp
RtPbrSurvey::SceneRendererSettings settings = renderer.CaptureSettings();

const std::string json = RtPbrSurvey::SerializeSceneRendererSettings(settings);

RtPbrSurvey::SceneRendererSettings restored;
std::string error;
if (RtPbrSurvey::DeserializeSceneRendererSettings(json, restored, &error))
{
    renderer.ApplySettings(restored);
}
```

Serialization includes a schema version. Missing fields use the values from a caller-provided defaults snapshot, allowing newer RtPbrSurvey builds to read older host files without discarding current defaults.

## Standalone App Integration

`App::SceneConfig` keeps application-owned camera, environment, scene, material, and animation state. Its renderer portion uses `SceneRendererSettings` and the Runtime JSON conversion rather than maintaining a second renderer schema.

The existing standalone scene configuration file layout remains readable. Renderer groups such as `lighting`, `shadow`, and `toneMap` remain at their existing scene-entry locations while their conversion is delegated to Runtime.

## Tank Integration

TankPhysicsSandbox can either save the serialized renderer settings as a standalone file or embed its JSON object in a larger Tank configuration document. A typical load point is after `SceneRenderer::Initialize()` and scene resource creation.

Tank should verify that changing and restoring `lighting.lightDirection` updates both direct lighting and RayQuery shadow direction.

## Test Strategy

Runtime tests cover:

- complete JSON round-trip
- defaults retained when fields are missing
- invalid JSON reporting without modifying the destination settings
- schema version presence

The standalone application build verifies integration with `App::SceneConfig`. Physical validation confirms that saved light and shadow parameters visibly restore after restart.
