# Scene Renderer Layer

## Goal

Make the RtPbrSurvey renderer usable from host applications such as TankPhysicsSandbox without carrying the whole sample app surface into the host. The first reusable layer is intentionally thin and backend-neutral.

## Current Reusable Parts

| Part | Path | Intended owner |
|------|------|----------------|
| `RtPbrSurvey::SceneRenderer` | `Runtime/SceneRenderer.*` | Host-facing renderer lifecycle and frame execution |
| `Engine::SceneBuilder` | `Scene/SceneBuilder.*` | Host-side scene construction for glTF and generated primitives |
| `RtPbrSurvey::DebugCameraController` | `Camera/DebugCameraController.*` | Optional debug/editor camera input helper |

## Host Integration Shape

A host app should own its platform window, message loop, ImGui frame, simulation state, and any product-specific tools. RtPbrSurvey should own renderer resources and scene submission through `SceneRenderer`.

The host currently owns `GraphicsDevice` and passes it to `SceneRenderer`. Use `RtPbrSurveyApp::OnInit()` as the reference setup:

```cpp
GraphicsDeviceDesc deviceDesc = {};
deviceDesc.hwnd = hwnd;
deviceDesc.swapChainWidth = width;
deviceDesc.swapChainHeight = height;
deviceDesc.bufferCount = RtPbrSurveyEngine::kSwapChainBufferCount;
deviceDesc.swapChainFormat = RtPbrSurveyEngine::kSwapChainFormat;
graphicsDevice.Initialize(deviceDesc);

RtPbrSurvey::SceneRenderer renderer(graphicsDevice);
renderer.Initialize(width, height);
```

For dynamic host state, prefer `SceneRenderer::SetUpdateHandler()`. The handler runs from `SceneRenderer::RunFrame()` before render passes consume the current scene. This makes it the right place for Tank to step physics, rebuild or update the host scene, and call `SetScene()`.

```cpp
renderer.SetUpdateHandler([&]() {
    debugCamera.UpdateFreeLookKeyboard(...);
    simulation.Step(...);

    sceneBuilder.Clear();
    // Fill sceneBuilder from simulation objects.
    // AddInstance accepts ordinary DirectXMath world matrices.
    // Use AddInstance(world, prevWorld, materialId) for moving objects.
    renderer.SetScene(sceneBuilder.GetScene());
});

renderer.RunFrame([&](ID3D12GraphicsCommandList* commandList) {
    imguiSystem.Render(commandList);
});
```

Call `ReloadSceneResources()` when the mesh, materials, textures, or instance capacity change. Reload keeps the previous visible instance count clamped to the new scene; if the count was zero, the new scene's instance count becomes visible by default. Per-frame transform-only updates should use `SetScene()` from the update handler after resources are loaded.

Use `SceneRenderer::SetToolUiHandler()` or draw host ImGui windows before `RunFrame()`'s UI render callback. Tank-specific panels can stay in Tank code; renderer debug UI can remain hidden by default and only be opened when needed.

## Naming

The public layer should stay backend-neutral. Use names such as `SceneRenderer`, `SceneBuilder`, and `DebugCameraController`; avoid `Dx12Renderer` for host-facing APIs so Vulkan or another backend can be introduced later without renaming the integration point.

## Suggested Next Steps

1. Keep `RtPbrSurveyApp` as the reference host, but avoid adding new renderer ownership there unless the same behavior is available through `SceneRenderer`.
2. Add a small Tank-facing sample or adapter only after the boundary above is stable enough to compile from TankPhysicsSandbox.
3. Move more app-only debug controls behind host-callable renderer/debug helpers when Tank needs them.
4. Treat physical input behavior as a manual verification item: orbit, pan, wheel, TAB mode switch, F1 debug UI, and Ctrl-click pixel pick.

## Current Manual Check List

- Scene select opens normally.
- `-AutoSelectGltfDamagedHelmet` starts in Running mode with renderer debug UI hidden.
- F1 toggles renderer debug UI.
- TAB toggles FreeLook and Arcball.
- Mouse orbit, pan, wheel, right-drag movement, and Ctrl-click pixel pick still work.
