# CMake Host Integration

## Purpose

RtPbrSurvey can be consumed by host applications such as TankPhysicsSandbox through CMake without modifying the RtPbrSurvey submodule. The first upstream CMake target is a static renderer-host library.

## Target

| Target | Purpose |
|--------|---------|
| `RtPbrSurvey::SceneRenderer` | Static library containing `SceneRenderer`, `SceneBuilder`, `DebugCameraController`, `ImGuiSystem`, renderer passes, runtime engine, scene helpers, and platform helpers needed by a minimal host. |

## Required Packages

The CMake path expects vcpkg packages for:

- `imgui` with `dx12-binding` and `win32-binding`
- `tinygltf`
- `nlohmann-json`

It also expects the existing NuGet packages under `packages/`:

- `Microsoft.Direct3D.D3D12.1.618.3`
- `Microsoft.Direct3D.DXC.1.8.2505.32`
- `WinPixEventRuntime.1.0.240308001`

Run `Restore-NuGet.ps1` in RtPbrSurvey if those package folders are missing.

## Host CMake Usage

```cmake
add_subdirectory(External/RtPbrSurvey)

add_executable(TankSandbox WIN32
    src/main.cpp
    src/TankSandboxApp.cpp)

target_link_libraries(TankSandbox PRIVATE RtPbrSurvey::SceneRenderer)
rtpbrsurvey_copy_runtime_files(TankSandbox)
```

`rtpbrsurvey_copy_runtime_files(<target>)` copies:

- `Assets/`
- compiled `.cso` shader files
- `D3D12/D3D12Core.dll`
- `D3D12/d3d12SDKLayers.dll`
- `dxcompiler.dll`
- `dxil.dll`
- `WinPixEventRuntime.dll`

## Minimal Host Flow

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

Engine::SceneBuilder builder;
const uint32_t material = builder.AddSolidColorMaterial(255, 255, 255, 255);
builder.AppendCube(1.0f, material);
builder.AddInstance(DirectX::XMMatrixTranslation(0.0f, 0.0f, 0.0f), material);

renderer.SetScene(builder.GetScene());
renderer.ReloadSceneResources(builder.GetScene());
renderer.SetUpdateHandler([&]() {
    renderer.SetScene(builder.GetScene());
});

renderer.RunFrame([&](ID3D12GraphicsCommandList* commandList) {
    imguiSystem.Render(commandList);
});
```

`SceneBuilder::AddInstance()` accepts ordinary DirectXMath world matrices and converts them to RtPbrSurvey's internal `InstanceData` storage layout.

`ReloadSceneResources()` keeps the visible instance count clamped to the new scene. If the count was zero, the new scene's instance count becomes visible by default.

## DirectX Header Note

RtPbrSurvey currently includes `include/d3dx12/d3dx12.h`, matching the Microsoft.Direct3D.D3D12 NuGet package layout. The CMake target adds both the NuGet native root and native include directory to its include paths. A future vcpkg `directx-headers` path may need a small forwarding include or a source include cleanup if that package is used without the NuGet package.
