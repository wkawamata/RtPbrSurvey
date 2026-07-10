# feature/scene-config-save-load

Per-scene save/load of camera position and ImGui parameters, with developer-provided defaults and user overrides.

## Files

| File | Purpose |
|------|---------|
| `Assets/Config/scene_config_default.json` | Developer-provided default values per scene (git-tracked) |
| `App/SceneConfig.h` | `SceneConfig` struct (aggregate of all per-scene state) + `SceneConfigManager` class |
| `App/SceneConfig.cpp` | JSON serialize/deserialize, file I/O, load cascade, apply to engine |
| `vcpkg.json` | Add `"nlohmann-json"` dependency |

### Modified Files

| File | Changes |
|------|---------|
| `App/RtPbrSurveyApp.h` | Add `SceneConfigManager m_sceneConfig` member |
| `App/RtPbrSurveyApp.cpp` | Integrate save/load in `OpenSelectedScene()`, `OnDestroy()`, `OnInit()` |
| `App/DebugUi.cpp` | "Scene Config" UI panel with Save/Load/Reset buttons |
| `RtPbrSurvey.vcxproj` | Add new source files |

## Two Config Files

| File | Location | Git | Purpose |
|------|----------|-----|---------|
| `scene_config_default.json` | `Assets/Config/` | Tracked | Developer defaults per scene |
| `scene_config.json` | `%APPDATA%\RtPbrSurvey\` | Ignored | User overrides (written on Save) |

### Load Cascade (scene load order)

1. Code defaults (`LoadSceneCpuData()` resets camera, mesh scale, etc.)
2. `scene_config_default.json` entry for this scene (if exists) -- apply
3. `scene_config.json` (AppData) entry for this scene (if exists) -- apply last (wins)

### Load Cascade (visual)

```
Code defaults
    |
    v
Default config (from Assets/Config/)
    |
    v
User config (from AppData/)  <-- wins if present
```

## What Is Saved (per scene)

### Camera

Arcball mode: `mode`, `yaw`, `pitch`, `distance`, `fov`
FreeLook mode: `mode`, `position[x,y,z]`, `rotation[x,y,z]`, `fov`

### Scene params

`meshScale`, `displayInstanceCount`, `selectedMaterialIndex`, `isPlaying`

### Lighting (`LightingParams`)

`lightDirection`, `lightColor`, `iblIntensity`, `diffuseIntensity`
Toggles: `directLightEnabled`, `diffuseIblEnabled`, `specularIblEnabled`, `emissiveEnabled`, `skyboxEnabled`, `skyboxPreview`, `skyboxPreviewExposure`

### Environment (`ProceduralEnvironmentSettings`)

`source` (enum), `skyColor`, `groundColor`, `lightColor`, `lightDirection`, `backgroundIntensity`, `lightIntensity`, `lightSize`, `fillIntensity`, `colorPanelIntensity`, `horizonSharpness`

Additional: `autoUpdate`, `iblEnabled` (master toggle)

### Tone mapping (`ToneMapParams`)

`operatorIndex`, `exposure`, `paperWhiteNits`, `maxDisplayNits`

### Rendering / Debug

`renderingPath` (Forward/Deferred), `renderViewMode`, `lightingPassDebugGradient`, `backBufferClearColor`

### Engine settings (via getter/setter)

**Shadow**: `enabled`, `normalBias`, `rayTMin`, `rayTMax`, `softShadowEnabled`, `sampleCount`, `lightAngularRadius`, `jitterStrength`

**Hybrid Reflection**: `enabled`, `materialGateEnabled`, `maxRoughness`, `minMetallic`, `hitOverlayEnabled`, `hitOverlayMode`, `hitOverlayIntensity`, `hitNormalSource`, `contributionEnabled`, `contributionIntensity`

**Specular Debug Lines**: `enabled`, `lineLength`, `showViewRay`, `showNormal`, `showReflection`

## Not Saved (v1)

- Material overrides (per-material roughness/metallic/AO/emissive)
  -- requires adding `GetMaterialParams()` readback to the engine

## JSON Schema (v1)

```json
{
  "version": 1,
  "scenes": {
    "DamagedHelmet": {
      "camera": {
        "mode": "arcball",
        "yaw": 0.785,
        "pitch": 0.349,
        "distance": 8.5,
        "fov": 60.0
      },
      "meshScale": 0.5,
      "displayInstanceCount": 1,
      "selectedMaterialIndex": 0,
      "isPlaying": false,
      "renderingPath": "deferred",
      "renderViewMode": "lightPass",
      "lightingPassDebugGradient": false,
      "backBufferClearColor": [0.0, 0.2, 0.4, 1.0],
      "iblEnabled": true,
      "lighting": {
        "lightDirection": [0.0, 1.0, -1.0],
        "lightColor": [1.0, 1.0, 1.0],
        "iblIntensity": 0.1,
        "diffuseIntensity": 1.0,
        "skyboxEnabled": true,
        "skyboxPreview": false,
        "skyboxPreviewExposure": 1.0,
        "directLightEnabled": true,
        "diffuseIblEnabled": true,
        "specularIblEnabled": true,
        "emissiveEnabled": true
      },
      "environment": {
        "source": "AssetHdr",
        "autoUpdate": true,
        "skyColor": [0.42, 0.56, 0.72],
        "groundColor": [0.18, 0.17, 0.15],
        "lightColor": [1.0, 0.96, 0.86],
        "lightDirection": [0.35, 0.75, 0.25],
        "backgroundIntensity": 0.6,
        "lightIntensity": 6.0,
        "lightSize": 0.12,
        "fillIntensity": 0.12,
        "colorPanelIntensity": 1.5,
        "horizonSharpness": 0.08
      },
      "toneMap": {
        "operatorIndex": 0,
        "exposure": 1.0,
        "paperWhiteNits": 300.0,
        "maxDisplayNits": 1000.0
      },
      "shadow": {
        "enabled": true,
        "normalBias": 0.01,
        "rayTMin": 0.001,
        "rayTMax": 10000.0,
        "softShadowEnabled": true,
        "sampleCount": 8,
        "lightAngularRadius": 0.1,
        "jitterStrength": 2.0
      },
      "hybridReflection": {
        "enabled": true,
        "materialGateEnabled": false,
        "maxRoughness": 0.35,
        "minMetallic": 0.0,
        "hitOverlayEnabled": false,
        "hitOverlayMode": 0,
        "hitOverlayIntensity": 0.2,
        "hitNormalSource": 0,
        "contributionEnabled": false,
        "contributionIntensity": 0.25
      },
      "specularDebugLines": {
        "enabled": true,
        "lineLength": 1.0,
        "showViewRay": true,
        "showNormal": true,
        "showReflection": true
      }
    },
    "Cornell Box + Mirror Ball": {
      "camera": {
        "mode": "freelook",
        "position": [1.5, 0.5, -3.0],
        "rotation": [0.0, 0.0, 0.0],
        "fov": 55.0
      }
    }
  }
}
```

## API: SceneConfigManager

```cpp
class SceneConfigManager {
public:
    // Load cascade: defaults -> user override, apply engine setters
    void LoadAndApplyForScene(int sceneIndex,
                              RtPbrSurveyApp& app,
                              RtPbrSurveyEngine& engine,
                              const Engine::SampleScene& scene);

    // Capture current state and write to user config
    void SaveCurrentScene(int sceneIndex,
                          const RtPbrSurveyApp& app,
                          const RtPbrSurveyEngine& engine,
                          const Engine::SampleScene& scene);

    // Reload defaults from disk (re-read, apply, preserve user config on disk)
    void LoadDefaultsForScene(int sceneIndex,
                              RtPbrSurveyApp& app,
                              RtPbrSurveyEngine& engine,
                              const Engine::SampleScene& scene);

    // Remove scene entry from user config, then reload defaults
    void ResetCurrentScene(int sceneIndex,
                           RtPbrSurveyApp& app,
                           RtPbrSurveyEngine& engine,
                           const Engine::SampleScene& scene);

    // Delete whole user config file, then reload defaults
    void ResetAllScenes(RtPbrSurveyApp& app,
                        RtPbrSurveyEngine& engine,
                        int currentSceneIndex,
                        const Engine::SampleScene& currentScene);

    // Human-readable source hint for UI display
    const char* ActiveSourceHint(const std::string& sceneName) const;

private:
    std::string m_defaultsPath;
    std::string m_userConfigPath;
    std::unordered_map<std::string, SceneConfig> m_defaults;
    std::unordered_map<std::string, SceneConfig> m_userOverrides;

    void ReadDefaultsFromDisk();
    void ReadUserConfigFromDisk();
    void WriteUserConfigToDisk();
    void ApplyToEngine(const SceneConfig& cfg,
                       RtPbrSurveyApp& app,
                       RtPbrSurveyEngine& engine);
    SceneConfig CaptureFromApp(const RtPbrSurveyApp& app,
                               const RtPbrSurveyEngine& engine);
    SceneConfig Merge(const SceneConfig& defaults,
                      const SceneConfig& overrides);
};
```

## Apply Order (critical for correctness)

1. `m_iblEnabled` -- master toggle; must be set before lighting
2. `m_environmentAutoUpdate` -- must be set before ReloadEnvironmentResources
3. `SetLightingParams()` -- honors the current iblEnabled state
4. `ReloadEnvironmentResources()` -- regenerates env maps on GPU
5. `SetToneMapParams()` -- independent
6. `SetShadowSettings()` -- independent
7. `SetHybridReflectionSettings()` -- independent
8. `SetSpecularDebugLineSettings()` -- independent
9. `SetRenderingPath()` -- may trigger PSO rebuild
10. `SetRenderViewMode()` -- may trigger PSO rebuild
11. `SetBackBufferClearColor()` -- trivial
12. `SetDisplayInstanceCount()` -- trivial
13. Camera vars (member assignment) -- applied next frame

After Apply, the screen reflects the new settings on the next frame. Each setter updates GPU state immediately (constant buffers, PSOs, env map textures).

## Lifecycle

| Trigger | Action |
|---------|--------|
| App startup (`OnInit`) | Load config for initial scene after `OpenSelectedScene()` |
| Scene switch (`OpenSelectedScene`) | Save outgoing -- LoadSceneCpuData -- Apply incoming |
| App exit (`OnDestroy`) | Save current scene |
| "Save Current" button | Capture -> WriteUserConfigToDisk |
| "Load Defaults" button | Re-read both files from disk, apply (user config kept) |
| "Reset Current Scene" button | Remove entry from user config, rewrite file, apply defaults |
| "Reset All Scenes" button | Delete user config file entirely, apply defaults |

## UI Layout

```
[>] Scene Config
    Current source: [User config / Default config / Code defaults]

    [Save Current]  [Load Defaults]
    -------------------------------------------------
    [Reset Current Scene]
    [Reset All Scenes]       (popup confirm)
```

### Reset All Scenes confirmation

```
[?] Reset all scene configurations to defaults?
    This cannot be undone.

    [OK]  [Cancel]
```

## File I/O Safety

User config writes use temp-file-then-rename for crash safety:

```
WriteToFile(tempPath);
MoveFileEx(tempPath, userConfigPath, MOVEFILE_REPLACE_EXISTING);
```

All reads re-read from disk (no in-memory cache staleness).

## Step 3 Survey: Getter/Setter Gaps

### Engine Getters (available)

| Getter | Returns | Notes |
|--------|---------|-------|
| `GetShadowSettings()` | `const ShadowSettings&` | Direct read |
| `GetHybridReflectionSettings()` | `const HybridReflectionSettings&` | Direct read |
| `GetSpecularDebugLineSettings()` | `const SpecularDebugLineSettings&` | Direct read |

### Engine Getters (missing, captured from App members)

| Parameter | App Member | Type |
|-----------|-----------|------|
| Lighting | `m_lightingParams` | `LightingParams` (struct) |
| ToneMap | `m_toneMapParams` | `ToneMapParams` (struct) |
| Rendering Path | `m_renderingPath` | `RenderingPath` (enum) |
| Render View Mode | `m_renderViewMode` | `RenderViewMode` (enum) |
| Debug Gradient | `m_lightingPassDebugGradient` | `bool` |
| BackBuffer Clear Color | `m_backBufferClearColor` | `array<float,4>` |
| IBL Enabled | `m_iblEnabled` | `bool` |
| Environment Settings | `m_environmentSettings` | `ProceduralEnvironmentSettings` (struct) |
| Environment Auto Update | `m_environmentAutoUpdate` | `bool` |

### Camera State (captured from App members)

| Camera Parameter | Source | Type |
|-----------------|--------|------|
| Camera mode (Arcball/FreeLook) | `m_cameraMode` enum via accessor | `CameraMode` |
| Arcball yaw | `m_objectViewerYaw` | `float` |
| Arcball pitch | `m_objectViewerPitch` | `float` |
| Arcball distance | `m_objectViewerDistance` | `float` |
| Arcball pivot | `m_objectViewerPivot` | `XMFLOAT3` |
| FreeLook position | `scene.camera.pos` (via `LoadedScene().GetScene()`) | `XMFLOAT3` |
| FreeLook rotation | `scene.camera.rot` | `XMFLOAT3` |
| FOV | `scene.camera.fov` | `float` |

### Engine Setters (all available)

All setters store member variables only. GPU state is updated in `RunFrame()` -> `RenderFrame()` where members are uploaded to constant buffers. Exception: `SetSpecularDebugLineSettings()` also calls `UpdateDebugLines()`.

| Setter | Effect |
|--------|--------|
| `SetLightingParams(LightingParams)` | Stores to `m_lightingParams` |
| `SetShadowSettings(ShadowSettings)` | Stores to `m_shadowSettings` |
| `SetHybridReflectionSettings(...)` | Stores to `m_hybridReflectionSettings` |
| `SetSpecularDebugLineSettings(...)` | Stores + calls `UpdateDebugLines()` |
| `SetRenderingPath(RenderingPath)` | Stores to `m_renderingPath` |
| `SetLightingPassDebugGradient(bool)` | Stores to `m_lightingPassDebugGradientEnabled` |
| `SetBackBufferClearColor(array)` | Stores to `m_backBufferClearColor` |
| `SetDisplayInstanceCount(int)` | Stores to `m_displayInstanceCount` |
| `SetToneMapParams(ToneMapParams)` | Writes to `m_toneMapPass.settings` |
| `SetRenderViewMode(RenderViewMode)` | Stores to `m_debugViewSettings.renderViewMode` |
| `ReloadEnvironmentResources(settings)` | Regenerates env maps on GPU |

### Apply Order (Critical)

```
1. m_environmentAutoUpdate   -- before ReloadEnvironmentResources
2. m_iblEnabled              -- affects how lighting is applied
3. SetLightingParams()       -- reads iblEnabled internally in DebugUi
4. ReloadEnvironmentResources() -- uses environment settings
5. SetShadowSettings()
6. SetHybridReflectionSettings()
7. SetSpecularDebugLineSettings()
8. SetToneMapParams()
9. SetRenderingPath()        -- may trigger PSO rebuild
10. SetRenderViewMode()
11. SetBackBufferClearColor()
12. SetDisplayInstanceCount()
13. Camera member assignment  -- takes effect next frame
```

### Conclusion

No new engine getters/setters needed for v1. All saveable state is readable from app member variables and existing engine getters. All setters exist and work by member storage (GPU updated next frame).

## Open Questions

- Material overrides: deferred to v2 (needs `GetMaterialParams()` readback on engine)
- Whether to add `Ctrl+S` / `Ctrl+R` keyboard shortcuts in v1
- glTF viewer and glTF grid benchmark scenes share the same name (e.g., "DamagedHelmet" appears twice in the scene list). In v1, both share the same config key. If separate configs are needed, the key can be disambiguated by checking whether the scene is within the grid-scene index range.
- Default config includes camera positions for all non-stub scenes (loaded assets + demos + Cornell Box). Stub scenes (FlightHelmet, Suzanne, BoxTextured, CesiumMan) are omitted.

## Step 4: Camera + Scene Params (done)

### Files changed

| File | Change |
|------|--------|
| `App/RtPbrSurveyApp.h` | Added `#include "SceneConfig.h"`, `friend class App::SceneConfigManager`, `m_sceneConfig` member |
| `App/RtPbrSurveyApp.cpp` | Wired `SetPaths()` in `OnInit()`, save in `OpenSelectedScene()`/`CloseRunningScene()`/`OnDestroy()`, load in `OpenSelectedScene()` |
| `App/SceneConfig.cpp` | Implemented `CaptureFromApp()` (camera + scene params), `ApplyToEngine()` (camera + scene params), `LoadAndApplyForScene()`, `SaveCurrentScene()`, `LoadDefaultsForScene()`, `ResetCurrentScene()`, `ResetAllScenes()` |
| `App/DebugUi.cpp` | Added "Scene Config" panel with Save/Load/Reset buttons + Reset All confirmation popup |

### Scope verified

- Only camera mode + arcball params (yaw/pitch/distance/pivot) + freelook params (pos/rot/fov) + meshScale + displayInstanceCount + selectedMaterialIndex + isPlaying are saved/loaded.
- Lighting, environment, tone map, shadow, hybrid reflection, specular debug lines, render path, render view mode are untouched.
- Build: 0 errors.

### UI (minimal)

```
[>] Scene Config
    Source: Default config / User config / Code defaults
    [Save Current]  [Load Defaults]
    [Reset Current Scene]  [Reset All Scenes]
```

Reset All uses an ImGui popup modal confirmation.

## Step 5: Lighting, Environment, ToneMap, Rendering / Debug, Engine State (done)

### Files changed

| File | Change |
|------|--------|
| `App/SceneConfig.cpp` | Added JSON serialization/deserialization for all remaining sub-configs (Lighting, Environment, ToneMap, Shadow, HybridReflection, SpecularDebugLines) + scene-level rendering/debug fields (renderingPath, renderViewMode, lightingPassDebugGradient, backBufferClearColor, iblEnabled, environmentAutoUpdate). Updated `CaptureFromApp()` to read all state from app members and engine getters. Updated `ApplyToEngine()` with the correct 12-step apply order (environmentAutoUpdate -> iblEnabled -> lighting -> environment/ReloadEnvironmentResources -> shadow -> hybridReflection -> specularDebugLines -> toneMap -> renderingPath -> renderViewMode -> lightingPassDebugGradient -> backBufferClearColor -> existing camera/scene-params steps). |
| `Assets/Config/scene_config_default.json` | Expanded from camera-only entries to full config entries for all 11 scenes (all fields). Cornell Box gets `[0.1, 0.1, 0.1, 1.0]` backBufferClearColor. |

### Apply order (enforced in ApplyToEngine)

1. `m_environmentAutoUpdate` -- must be set before env reload
2. `m_iblEnabled` -- master toggle before lighting
3. `app.m_lightingParams` (all fields) -- synced to engine each frame
4. `engine.ReloadEnvironmentResources()` -- GPU env map regeneration
5. `engine.SetShadowSettings()` -- engine direct setter
6. `engine.SetHybridReflectionSettings()` -- engine direct setter
7. `engine.SetSpecularDebugLineSettings()` -- engine direct setter
8. `app.m_toneMapParams` (all fields) -- synced to engine each frame
9. `app.m_renderingPath` -- synced to engine each frame
10. `app.m_renderViewMode` -- synced to engine each frame
11. `app.m_lightingPassDebugGradient` -- synced to engine each frame
12. `app.m_backBufferClearColor` -- synced to engine each frame

### Capture sources

| Config section | Source |
|----------------|--------|
| Lighting | `app.m_lightingParams` (XMFLOAT3 -> array) |
| IBL master | `app.m_iblEnabled` |
| Environment | `app.m_environmentSettings` (source as int, XMFLOAT3 -> array) |
| Environment auto-update | `app.m_environmentAutoUpdate` |
| Tone map | `app.m_toneMapParams` |
| Rendering path | `app.m_renderingPath` (enum -> int) |
| Render view mode | `app.m_renderViewMode` (enum -> int) |
| Debug gradient | `app.m_lightingPassDebugGradient` |
| Clear color | `app.m_backBufferClearColor` |
| Shadow | `engine.GetShadowSettings()` |
| Hybrid reflection | `engine.GetHybridReflectionSettings()` |
| Specular debug lines | `engine.GetSpecularDebugLineSettings()` |

### Migration note

User config files saved by Step 4 only contain camera + scene-params fields. Loading such a file with Step 5 code will fill missing sections with struct defaults (lighting, environment, etc. reset). To migrate, save each scene again after upgrading (Save captures all fields).

### Build

0 errors.

### Step 5 review fix

iblDebugMip and iblDebugExposure are included in lighting config serialization, capture, apply, and default scene config entries.
