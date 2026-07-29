# Shared Environment Mapping Debug UI

External hosts can add the renderer-owned Environment Mapping controls to the existing debug window:

```cpp
RtPbrSurvey::EnvironmentMappingUiState environmentUi;

// Keep environmentUi alive with the host scene/debug state.
RtPbrSurvey::SceneRendererDebugUi::Draw(
    sceneRenderer,
    &debugWindowOpen,
    "RtPbrSurvey Debug",
    &environmentUi);
```

`EnvironmentMappingUiState` owns the editable procedural environment settings, lighting/IBL settings,
master IBL enable, auto-update policy, and pending reload flag. The host decides whether and how to persist this
state. RtPbrSurvey does not perform host configuration file I/O.

The same controls can be composed into another already-open ImGui window:

```cpp
if (ImGui::CollapsingHeader("Environment Mapping"))
{
    RtPbrSurvey::SceneRendererDebugUi::DrawEnvironmentMapping(sceneRenderer, environmentUi);
}
```

The shared UI owns the source presets, slider ranges, and calls to
`SceneRenderer::ReloadEnvironmentResources()`. Procedural edits reload after the active ImGui edit completes
when Auto Update is enabled. With Auto Update disabled, the host uses the displayed Apply Environment button.

## Asset HDR contract

Selecting Asset HDR requests `Assets/Environment/default_environment.hdr`. Although GPU procedural generation
is enabled by default, `ReloadEnvironmentResources()` deliberately routes Asset HDR through the CPU HDR loading
and cubemap conversion path. The procedural GPU path is used only for non-Asset sources. If loading or cubemap
creation fails, the renderer logs the failure and falls back to Procedural Sun.
