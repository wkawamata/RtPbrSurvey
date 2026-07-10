# RtPbrSurvey

A real-time Physically Based Rendering (PBR) survey tool for DirectX 12 that explores, compares, and debugs modern real-time rendering techniques including deferred/forward PBR, DXR RayQuery shadows, hybrid ray-traced reflections, image-based lighting, and procedural environment maps.

## Features

- Deferred rendering with 6 GBuffer targets (Albedo, Normal, Material, MotionVector, PBRParams, Emissive)
- Forward rendering path
- Image-Based Lighting (IBL): HDR environment maps, diffuse irradiance convolution, specular prefiltered environment, BRDF LUT
- GPU-generated procedural environment maps (5 sources: Studio, Sun, ColorPanels, Horizon, AssetHdr)
- DXR RayQuery soft shadows (1-16 samples, configurable bias/radius/jitter)
- DXR RayQuery hybrid reflections (material gating, hit overlays, debug modes)
- Tone mapping (None/Reinhard/ACES) and HDR10 output with ST.2084 PQ
- 22 debug visualization modes (GBuffer, light pass, IBL, ray tracing, reflection debug)
- Pixel Pick inspector (Ctrl+Click) with specular debug line visualization
- WorkMeter CPU/GPU per-pass profiling
- Full ImGui-based parameter control
- CLI automation (-LogToFile, -LogFPS, -AutoSelectGltfDamagedHelmet, -warp)

## Scenes

- **glTF Viewer**: DamagedHelmet, Avocado, BoomBox, Lantern, Sponza (FlightHelmet, Suzanne, BoxTextured, CesiumMan are stubs pending asset availability)
- **glTF Grid Benchmark**: Supported models (DamagedHelmet, Avocado, BoomBox, Lantern, Sponza) rendered in instanced grids (up to 1000 instances)
- **Demo Scenes**: Metallic Roughness Sphere, Shadow Test Ground Cubes, Animated Shadow Grid, Contact Shadow Test, Occluder Wall Test, Cornell Box + Mirror Ball

## Controls

| Action | Input |
|--------|-------|
| Orbit (Arcball) / Look (FreeLook) | Left mouse drag |
| Pan | Middle mouse drag |
| Dolly | Mouse wheel |
| Zoom (FOV) | Ctrl + Mouse wheel |
| Move (FreeLook) | W/A/S/D, Shift for up/down |
| Toggle camera mode | Tab |
| Pixel inspect (deferred) | Ctrl + Left Click |
| Pause animation | Space |
| Return to scene select | Escape |

## Build

Prerequisites: Visual Studio 2022, Windows 10 SDK, vcpkg, nuget.exe.

See [BUILD.md](BUILD.md) for full instructions.

Quick start:

```powershell
.\Restore-NuGet.ps1
msbuild RtPbrSurvey.sln /p:Configuration=Debug /p:Platform=x64
```

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `App/` | Application lifecycle, scene selection, debug UI |
| `Engine/` | Core engine orchestration, render pass graph, frame execution |
| `Renderer/` | Individual render passes, pipeline/root signature helpers, GPU resource management |
| `Scene/` | Scene data, sample scenes, glTF/procedural scene definitions |
| `Shaders/` | HLSL shader sources and shared include files |
| `Shared/` | Cross-cutting data types shared by CPU/GPU |
| `Platform/` | OS abstraction (window, command line, file I/O) |
| `Ui/` | ImGui system integration |
| `Assets/` | Runtime assets (glTF models, HDR environment maps) |
| `doc/` | Design notes, render pass authoring guide, test procedures |

## Dependencies

- [imgui](https://github.com/ocornut/imgui) (vcpkg, with dx12-binding and win32-binding)
- [tinygltf](https://github.com/syoyo/tinygltf) (vcpkg)
- DirectXMath (Windows SDK)
- DirectX headers (NuGet)

## Architecture

The engine is built on a declarative render pass graph that automatically manages resource lifetimes, transitions, and transient resource creation. Each render pass declares its pipeline state, inputs, outputs, and recording operation. The graph framework resolves dependencies, analyzes lifetimes, and executes passes in order each frame.

## License

See copyright headers in source files for licensing details.

## See Also

- [BUILD.md](BUILD.md) - Detailed build instructions
- [RenderPassAuthoring.md](Renderer/RenderPassAuthoring.md) - Guide for adding/modifying render passes
