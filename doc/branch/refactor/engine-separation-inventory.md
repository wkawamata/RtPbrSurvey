# Engine Separation Step 1 Inventory

この文書は `doc/branch/refactor/engine-separation.md` の Step 1 成果物です。

目的は、既存コードを App / Platform / Engine / Transitional / ThirdParty の所有者で分類し、次に小さく切り出せる境界を明確にすることです。
この段階では、コード移動や rename は行いません。

## Ownership Categories

* App
  * HelloTexture というアプリケーション固有の振る舞い
  * scene selection
  * camera input
  * debug UI
  * demo / benchmark / test scene の構成

* Platform
  * Win32 window
  * message loop
  * command line parsing
  * file path / file I/O
  * HWND を必要とする実行環境依存処理

* Engine
  * D3D12 rendering
  * render pass / frame graph
  * scene runtime data
  * asset loading
  * material data
  * ray tracing support

* Transitional
  * 現在は複数責務を持つが、最終所有者を分ける必要があるもの
  * `D3D12HelloTexture`
  * `DXSample`
  * `GraphicsDevice`
  * `ImGuiSystem`

* ThirdParty
  * external library / package integration

## File-Level Inventory

| Item | Current File(s) | Current Owner | Proposed Owner | Notes |
|------|-----------------|---------------|----------------|-------|
| `Main.cpp` / `WinMain` | `Main.cpp` | App | App | `SampleApp` を作り `Win32Application::Run` に渡すだけの entry point。 |
| `Win32Application` | `Win32Application.h/.cpp` | Platform | Platform | Window class registration, `CreateWindow`, message loop, Win32 message dispatch。 |
| `DXSample` | `DXSample.h/.cpp` | Transitional | Platform + App interface | CLI, asset path, window title, adapter selection, virtual sample lifecycle が混在。最初の分割候補。 |
| `DXSampleHelper.h` | `DXSampleHelper.h` | Transitional | Platform + Engine + Shared | file I/O, DDS read, shader compile, D3D12 debug naming, HRESULT helper が混在。 |
| `SampleApp` | `SampleApp.h/.cpp` | App | App | scene lifecycle, camera, input, debug UI, logging, engine orchestration。 |
| `D3D12HelloTexture` | `D3D12HelloTexture.h/.cpp` | Transitional | Engine/Render | 現在の monolithic renderer。最終 Engine 名に rename せず、内部責務を抜く。 |
| render pass setup | `D3D12HelloTexturePasses.cpp` | Engine | Engine/Render + Engine/FrameGraph | pass graph construction と pass operation wiring。 |
| `GraphicsDevice` | `GraphicsDevice.h/.cpp` | Transitional | Platform/Render bridge | D3D12 device/queue/swapchain/fence。`HWND` を受けて swapchain を作る。 |
| `Renderer/*` | `Renderer/*` | Engine | Engine/Render | ほぼ全て Engine。App / DXSample / ImGui への直接依存はない。 |
| frame graph files | `Renderer/RenderPass*.h` | Engine | Engine/FrameGraph | `RenderPassKeys`, `RenderPassGraph`, `RenderPassExecution`, `RenderPassResources`。 |
| ray tracing files | `Renderer/Ray*`, `Renderer/*Reflection*`, `Renderer/AccelerationStructureResources.*` | Engine | Engine/RayTracing | Optional feature として Render の sub-module に分ける候補。 |
| `HdrOutput` | `Renderer/HdrOutput.*` | Engine | Platform or Platform callback | `HWND` / `GetWindowRect` を使うため Platform coupling がある。 |
| `Scene/Scene.h` | `Scene/Scene.h` | Engine | Engine/Scene + Engine/Asset | runtime scene と CPU asset container が混在。`DXSampleHelper.h` include は除去候補。 |
| `SampleScene` abstract interface | `Scene/SampleScene.h/.cpp` | Engine | Engine/Scene | app-defined scenes を載せる抽象境界として有用。 |
| concrete sample scenes | `Scene/SampleScene.h/.cpp`, `Scene/SceneFactory.cpp` | Engine | App | glTF viewer, benchmark grid, shadow test, Cornell box などはアプリ固有 content。 |
| `ProceduralSceneBuilder` | `Scene/ProceduralSceneBuilder.*` | Engine | Engine/Asset | reusable procedural mesh builder。 |
| `GltfLoader` | `GltfLoader.h/.cpp`, `TiniGgltfImpl.cpp` | Engine / ThirdParty | Engine/Asset + ThirdParty | loader wrapper は Asset、tiny_gltf implementation は ThirdParty。 |
| `CubeMesh` | `CubeMesh.h/.cpp` | Engine | Engine/Asset | procedural asset generator。 |
| `TextureSemantic` | `TextureSemantic.h` | Engine | Engine/Asset | CPU-side texture semantic enum。 |
| `Renderer/Material.*` | `Renderer/Material.h`, `Renderer/MaterialBuffer.*` | Engine | Engine/Material | GPU material representation and upload buffer。 |
| shader include files | `Material.hlsli`, `InstanceData.hlsli` | Engine | Engine/Shader | C++ layout と同期が必要。 |
| `Ui/ImGuiSystem` | `Ui/ImGuiSystem.*` | Transitional | App/Platform/Engine bridge | DX12 + Win32 ImGui backend wrapper。`HWND` が必要。 |
| `ImGuiWidgets.h` | `ImGuiWidgets.h` | App | App | debug UI 用 helper。 |
| `WorkMeter` | `WorkMeter.h/.cpp` | Engine | Engine/Diagnostics | CPU/GPU profiling。`OutputDebugStringA` 系は Platform diagnostic に逃がす余地あり。 |
| `MyDx12Utils.h` | `MyDx12Utils.h` | Transitional | Engine + Shared | upload buffer, scoped timer, GPU timestamp などが混在。 |

## Dependency Findings

### Renderer

`Renderer/*` は概ね良い状態です。
OpenCode 調査では、`Renderer/` 配下の主要ファイルは `RtPbrSurveyEngine`, `SampleApp`, `DXSample`, `Win32Application`, ImGui header に直接依存していません。

例外は以下です。

* `Renderer/AccelerationStructureResources.h`
  * `Scene/Scene.h` を include して `InstanceData` に依存している
  * `InstanceData` を shared header へ移す、または header では forward declaration に寄せる候補

* `Renderer/HdrOutput.h/.cpp`
  * `HWND` と Win32 display query に依存している
  * Platform へ移す、または Platform callback を注入する候補

### Scene / Asset

`Scene/` は名前よりも広い責務を持っています。

* `Scene.h` には runtime scene data と CPU asset container が混在している
* `Scene.h` が `DXSampleHelper.h` を include している主因は `UINT`
* `uint32_t` に変えるだけで Platform / Windows include 依存を外せる可能性が高い
* concrete sample scenes は Engine より App content として扱う方が自然

### App / UI

`SampleApp` は App として正しい中心ですが、かなり大きくなっています。

* debug UI が `SampleApp.cpp` に集中している
* camera input / object viewer / arcball logic も App に集中している
* renderer debug view, GBuffer fields, pixel pick result など Engine 内部寄りの概念を直接表示している
* D3D12 descriptor heap for ImGui と `ID3D12InfoQueue` 管理が App に露出している

### Platform

`Win32Application` は Platform としてかなり独立しています。
一方で `DXSample` と `DXSampleHelper.h` は Platform / Engine / Shared が混ざっています。

最初に整理するなら `DXSample` と `DXSampleHelper.h` が最も効果的です。

## Proposed First Small Refactor Candidates

### Candidate 1: Remove Windows dependency from scene/material data headers

目的:

* Engine data headers から Sample helper / Windows include を減らす
* move なしで dependency direction を良くする

対象:

* `Scene/Scene.h`
* `Renderer/Material.h`

内容:

* `DXSampleHelper.h` include を避ける
* `UINT` を `uint32_t` に置き換える
* 必要なら `<cstdint>` を include する

リスク:

* HLSL layout と C++ layout の同期確認が必要
* `InstanceData` / `Material` のサイズ確認を build で見る

### Candidate 2: Split `DXSampleHelper.h` by responsibility

目的:

* Platform file I/O と Engine D3D12 helper を分離する

分割候補:

* Platform
  * `GetAssetsPath`
  * `ReadDataFromFile`
  * file I/O part of `ReadDataFromDDSFile`

* Engine
  * `SetName`
  * `SetNameIndexed`
  * `CompileShader`
  * `CalculateConstantBufferByteSize`

* Shared
  * `HrException`
  * `ThrowIfFailed`

リスク:

* include surface が広いため、最初は移動ではなく新 header 追加 + include 置換を小さく行う

### Candidate 3: Extract App debug UI file

目的:

* `SampleApp.cpp` の巨大化を抑える
* App 層の中で UI と application orchestration を分ける

対象:

* `SampleApp::DrawDebugUi`
* `SampleApp::DrawSceneSelectUi`
* `ImGuiWidgets.h`

リスク:

* 参照する member が多い
* 先に `UiState` / `FrameSettings` の形を決めないと引数が増えすぎる

### Candidate 4: Define `Engine/FrameGraph` boundary

目的:

* すでに独立している render graph infrastructure を明確にする

対象:

* `Renderer/RenderPassKeys.h`
* `Renderer/RenderPassGraph.h`
* `Renderer/RenderPassExecution.h`
* `Renderer/RenderPassResources.h`

リスク:

* move は project file / filters 更新が必要
* 最初は namespace/comment/documentation だけでもよい

## Recommended Next Step

最初の実装は Candidate 1 が良いです。

理由:

* 変更範囲が小さい
* App / Platform / Engine 分離の方向に直接効く
* move を伴わない
* build で確認しやすい
* `Scene/Scene.h` と `Renderer/Material.h` の Windows / Sample helper 依存を減らせる

実装後に確認すること:

* Debug x64 build
* `rg "DXSampleHelper.h" Scene Renderer -g "*.h" -g "*.cpp"` で不要 include が減ったこと
* `InstanceData` と shader layout の意図が崩れていないこと

## OpenCode Reports

この inventory は以下の OpenCode research reports を統合したものです。

* `C:\work\DirectX-Graphics-Samples-agents\engine-separation-step1-platform-inventory\report.md`
* `C:\work\DirectX-Graphics-Samples-agents\engine-separation-step1-app-ui-inventory\report.md`
* `C:\work\DirectX-Graphics-Samples-agents\engine-separation-step1-renderer-inventory\report.md`
* `C:\work\DirectX-Graphics-Samples-agents\engine-separation-step1-scene-asset-inventory\report.md`
