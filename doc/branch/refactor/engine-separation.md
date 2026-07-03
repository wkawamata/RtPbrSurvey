# DirectX-Graphics-Samples 依存から独立した App / Engine / Platform 分離方針

目的は、Sample の実装をそのまま Engine に移すことではありません。
目的は、DirectX-Graphics-Samples のアプリケーションフレームワーク依存を段階的に外し、HelloTexture を独立したアプリケーションとして成立させることです。

重要: 原則、動作は変更しない。

既存の Sample 由来コードは、必要な間はコピーして使ってよいです。
ただし、所有境界を App / Engine / Platform に分け、Sample 由来の型名・継承・呼び出し構造を徐々に置き換えます。

## 目指す構造

最終的には以下の形を目指します。

```txt
App/
 HelloTextureApp/
Platform/
 Win32/
 Time/
 Input/
 FileSystem/
Engine/
 Core/
 Render/
 Scene/
 Material/
 Asset/
 FrameGraph/
 RayTracing/
ThirdParty/
```

この構造での役割は以下です。

* App
  * HelloTexture というアプリケーション固有の所有者
  * 起動設定
  * シーン選択
  * Debug UI の構成
  * 入力をどの操作に割り当てるか
  * Engine をどのように使うか

* Platform
  * Win32 entry point
  * Window 作成
  * Message loop
  * High resolution timer
  * OS input
  * ファイルパス解決
  * SwapChain の外側にある実行環境依存処理

* Engine
  * D3D12 device / queue / allocator / descriptor 管理
  * Render resource lifetime
  * Render pass / FrameGraph
  * Scene / Mesh / Material / Texture / Asset loading
  * Lighting / ToneMap / GBuffer
  * RayTracing / TLAS / BLAS / RayQuery

## 基本方針

`Engine` を「Sample から逃がした置き場」にしない。

`D3D12HelloTexture` の巨大な責務を `HelloTextureEngine` に丸ごと移すと、名前が変わっただけで構造は Sample のままになります。
移行の目的は、Sample のクラスを別名にすることではなく、責務の所有者を正しく分けることです。

そのため、当面の `D3D12HelloTexture` / `SampleApp` は compatibility shell として扱います。
ここから `Renderer/`, `Scene/`, `Asset/`, `Platform/`, `App/` へ責務を少しずつ抜き出します。

## 既存 Sample コードの扱い

DirectX-Graphics-Samples から独立するために、最初からすべてを書き直す必要はありません。

`DXSample`, `Win32Application`, `StepTimer`, `DXSampleHelper` などは、まず Sample 由来の暫定 Platform 層としてコピーして使ってよいです。
ただし、長期的には以下を進めます。

1. Sample 由来の型を Platform / App のどちらの責務かに分類する
2. `DXSample` 継承モデルに依存している箇所を明示する
3. App が必要とする Platform API を小さく定義する
4. `DXSample` / `Win32Application` / `StepTimer` を直接参照する範囲を狭める
5. 最後に Sample 由来の型名と継承構造を置き換える

最初に捨てるべきなのは描画コードではなく、Sample の起動・継承・呼び出しモデルへの依存です。

## App から見える最小 API

最終的に App 側は、Engine の内部 pass や D3D12 resource の詳細を直接扱わない形を目指します。

例:

```cpp
app.Initialize(platformContext);
app.LoadDefaultScene();

while (platform.PumpMessages())
{
    FrameInput input = platform.BeginFrame();
    app.RunFrame(input);
    platform.EndFrame();
}

app.Shutdown();
```

または、Engine を直接見る場合でも、App 側の呼び出しは以下程度に抑えます。

```cpp
engine.Initialize(initDesc);
engine.LoadScene(sceneDesc);
engine.SetDebugUiHandler(...);

while (running)
{
    engine.RunFrame(frameInput);
}

engine.Shutdown();
```

重要なのは、App が Render pass の構成、GBuffer の中身、Lighting / ToneMap の実装詳細を知らないことです。

## 移行ステップ

各ステップごとに必ずビルド可能・起動可能な状態を保ちます。

### Step 1: 現在の所有者を分類する

大きな移動はまだしません。
まず、既存のファイル・クラス・関数を以下の所有者で分類します。

* App
  * `SampleApp`
  * scene selection
  * debug UI composition
  * user-facing mode switching

* Platform
  * `Main.cpp`
  * `Win32Application`
  * `DXSample`
  * `StepTimer`
  * command line parsing
  * OS window / message loop

* Engine
  * `Renderer/*`
  * `Scene/*`
  * `GltfLoader`
  * material / texture semantics
  * render pass graph
  * D3D12 resource lifetime
  * ray tracing support

* Transitional
  * `D3D12HelloTexture`
  * `D3D12HelloTexturePasses`
  * ImGui integration
  * camera operation
  * debug draw helpers

成果物として、この文書に分類表を追加します。
別ファイルを作る場合は `doc/branch/refactor/engine-separation-inventory.md` とします。

### Step 2: App / Platform の外形を作る

`DXSample` 由来の起動経路をいきなり捨てず、まず Platform 層として囲います。

この段階では、以下を明確にします。

* App が Platform から受け取るもの
* Platform が App に渡すもの
* App が Engine に渡すもの
* Engine が App に返すもの

このステップでは、描画の見た目を変えません。

### Step 3: `D3D12HelloTexture` を compatibility shell に寄せる

`D3D12HelloTexture` を最終的な Engine クラスと見なさない。

`D3D12HelloTexture` に残っている処理を、以下の方向で少しずつ外へ出します。

* Render pass の実装は `Renderer/` に置く
* Scene / Mesh / Material / Texture の所有は `Scene/` / `Engine/Asset` に寄せる
* App 固有の UI と scene selection は `App/HelloTextureApp` に寄せる
* Win32 / command line / timer は `Platform/` に寄せる

### Step 4: Renderer を Sample 非依存にする

Renderer は以下に依存しない方向へ寄せます。

* `DXSample`
* `D3D12HelloTexture`
* Win32 `HWND`
* ImGui の具体実装
* Sample 固有の scene selection

Renderer が受け取るものは、明示的な desc / context / resource handle にします。

### Step 5: Sample 由来名を最後に消す

`D3D12HelloTexture*`, `DXSample*`, `SampleApp*` などの名前は、構造が分かれてから置き換えます。
最初に名前だけ変えると、責務の混在が見えにくくなります。

## やらないこと

当面、以下はやりません。

* `D3D12HelloTexture` を丸ごと `HelloTextureEngine` に rename する
* Sample の巨大クラスをそのまま Engine に移す
* 起動経路と描画経路を同じ変更で大きく書き換える
* RayTracing / Hybrid Reflection / Path Tracing の実装追加と分離作業を混ぜる
* 動作確認できない大規模移動を行う

## 確認項目

変更後は、必要に応じて以下を確認します。

* ビルドが通る
* 起動する
* 既存シーンが表示される
* ImGui が表示される
* Resize が壊れていない
* GBuffer Debug 表示が壊れていない
* D3D12 Debug Layer に `[ERROR]` が出ていない

Debug Layer 確認には、Debug build と CLI automation を使います。

```powershell
.\bin\x64\Debug\D3D12HelloTextureModified.exe -AutoSelectGltfDamagedHelmet -LogToFile d3d12_debug.log -LogFPS 120
```

```powershell
Select-String -LiteralPath d3d12_debug.log -Pattern "\[ERROR\]|\[WARNING\]|D3D12"
```

生成された `d3d12_debug.log` は commit しません。

## 注意点

RayTracing / Hybrid Reflection / Path Tracing に入る前に、この分離を進めます。

ただし、完全な独自フレームワーク化を急ぐ必要はありません。
今の目的は、Sample の形に縛られず、Engine 側に機能を追加できる状態にすることです。

## Standalone repository extraction policy

最終的には、`HelloTextureModified` を DirectX-Graphics-Samples の他サンプルから分け、独立した repository にする。

ただし、単純なファイルコピーではなく、Git の履歴を残したまま切り出す。
基本方針は、切り出し時に `git filter-repo` で `HelloTextureModified` の path だけを残し、その path を repository root に rename すること。

例:

```powershell
git clone <DirectX-Graphics-Samples repository> HelloTextureStandalone
cd HelloTextureStandalone

git filter-repo `
  --path Samples/Desktop/D3D12HelloWorld/src/HelloTextureModified/ `
  --path-rename Samples/Desktop/D3D12HelloWorld/src/HelloTextureModified/:
```

この方法なら、`HelloTextureModified` 配下に対して積んだ commit 履歴は standalone repository 側にも残る。

### Standalone 化までに守ること

* できるだけ必要な source / shader / asset / doc を `HelloTextureModified` 配下へ集約する。
* DirectX-Graphics-Samples の外側 path への相対参照を増やさない。
* `DXSample`, `Win32Application`, `StepTimer`, helper 類は、必要な間はコピー済み Platform 層として扱う。
* 共有 sample framework を参照する方向ではなく、この repository 内の Platform / App / Engine 境界へ寄せる。
* `HelloTextureModified` 配下で意味のある小さな commit を積む。
* 生成物や log は commit しない。

### 切り出し前に確認すること

* project file が `HelloTextureModified` の外側にある source / props / targets / assets を参照していないか。
* NuGet / vcpkg / package 参照が standalone repository で復元できるか。
* shader build rule が standalone root からも成立するか。
* `Assets/`, `D3D12/`, `packages`, `vcpkg_installed` など runtime / build time dependency の扱いを決める。
* Debug x64 build が standalone repository で通るか。
* CLI automation の `-AutoSelectGltfDamagedHelmet`, `-LogToFile`, `-LogFPS` が standalone repository でも動くか。
* D3D12 Debug Layer check で `[ERROR]` が出ないか。

### 今はやらないこと

* まだ standalone repository を手作業コピーで作らない。
* 履歴を失う形で source を別 repository に移植しない。
* DirectX-Graphics-Samples 直下の他 sample を巻き込んだ大規模 rename / move をしない。

まずは現在の repository 内で、`HelloTextureModified` 配下の所有境界を整理し、切り出しやすい状態を作る。

## 進捗

* Step 1 inventory を `doc/branch/refactor/engine-separation-inventory.md` に作成。
  * App / Platform / Engine / Transitional / ThirdParty の所有者分類を整理。
  * OpenCode research reports 4 件を統合。
  * 次の小さな refactor 候補として、Scene / Material data headers から Sample helper / Windows 型依存を減らす作業を選定。

## Commit 単位の進捗

### Scene / Material data headers から Sample helper 依存を削減

目的:

* `Scene/Scene.h` と `Renderer/Material.h` を Sample framework / Windows helper から少し独立させる。
* コード移動なしで、Engine data header の依存方向を改善する。

変更:

* `Scene/Scene.h`
  * `DXSampleHelper.h` include を削除。
  * `<cstdint>` を include。
  * `InstanceData::materialId` を `UINT` から `uint32_t` に変更。

* `Renderer/Material.h`
  * `DXSampleHelper.h` include を削除。
  * `<cstdint>` を include。
  * material constants と `Material` fields を `UINT` から `uint32_t` に変更。

確認:

* `git diff --check` OK。
* 対象ファイルの EOL は CRLF。
* Debug x64 build 成功。

メモ:

* HLSL layout と対応する構造体なので、今後も `InstanceData` / `Material` のサイズ・layout 変更には注意する。
* build 時に既存 warning は残るが error は 0。

### Material structured buffer layout guard を追加

目的:

* `Renderer/Material.h` の `uint32_t` 化後も、`Material.hlsli` と C++ 側の layout がずれた場合に compile time で検出する。

変更:

* `D3D12HelloTexture.cpp`
  * `static_assert(sizeof(Engine::Material) == 44, ...)` を追加。
  * 既存の `SceneVertex` / `InstanceData` layout guard と同じ場所に置いた。

確認:

* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。

### AccelerationStructureResources header の Scene 依存を削減

目的:

* `Renderer/AccelerationStructureResources.h` が `Scene/Scene.h` 全体を include しないようにする。
* Renderer public header の Scene 依存を細くし、RayTracing module の境界を明確にする。

変更:

* `Renderer/AccelerationStructureResources.h`
  * `Scene/Scene.h` include を削除。
  * `struct InstanceData;` の前方宣言に変更。

* `Renderer/AccelerationStructureResources.cpp`
  * `SceneVertex` と `InstanceData` の定義が必要なため、`Scene/Scene.h` include を `.cpp` 側へ移動。

確認:

* `git diff --check` OK。
* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。

### AccelerationStructureResources header の Sample helper 直接依存を削減

目的:

* `Renderer/AccelerationStructureResources.h` が `DXSampleHelper.h` の `ComPtr` using に直接依存しないようにする。
* RayTracing resource header が必要な D3D12 / WRL 型を明示的に include する。

変更:

* `Renderer/AccelerationStructureResources.h`
  * `DXSampleHelper.h` include を削除。
  * `<d3d12.h>` と `<wrl/client.h>` を include。
  * member resource fields を `ComPtr<ID3D12Resource>` から `Microsoft::WRL::ComPtr<ID3D12Resource>` に変更。

確認:

* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。

メモ:

* `SimpleDescriptorHeapAllocator.h` 経由の Sample helper 依存はまだ残っている。
* 次に進めるなら descriptor allocator header の自立化を検討する。

### SimpleDescriptorHeapAllocator header の Sample helper 依存を削減

目的:

* `Renderer/SimpleDescriptorHeapAllocator.h` が `DXSampleHelper.h` と `MyDx12Utils.h` に依存しないようにする。
* Descriptor allocator を Renderer 内の自立した utility header に近づける。

変更:

* `Renderer/SimpleDescriptorHeapAllocator.h`
  * `DXSampleHelper.h` include を削除。
  * `MyDx12Utils.h` include を削除。
  * `<d3d12.h>` と `<climits>` を直接 include。
  * `DBG_PRINT` を使った debug allocation log を削除。

* `Renderer/AccelerationStructureResources.cpp`
  * `ThrowIfFailed` は `.cpp` 内部だけで使うため、`DXSampleHelper.h` include を `.cpp` 側に明示。

確認:

* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。

### Small Renderer headers の Sample helper 依存を削減

目的:

* 定数・軽量 D3D12 型だけを公開する Renderer header が `DXSampleHelper.h` に依存しないようにする。
* `Renderer/` の public header surface を少しずつ自立させる。

変更:

* `Renderer/RootSignatureLayout.h`
  * `DXSampleHelper.h` include を削除。
  * `<cstdint>` を include。
  * root signature constants を `UINT` から `uint32_t` に変更。

* `Renderer/ResolvedRenderTargets.h`
  * `DXSampleHelper.h` include を削除。
  * `<d3d12.h>` を直接 include。

* `Renderer/FullscreenTriangle.h`
  * `DXSampleHelper.h` include を削除。
  * `<d3d12.h>` を直接 include。

* `Renderer/DebugLinePass.h`
  * `DXSampleHelper.h` include を削除。
  * 既存の `<d3d12.h>` / WRL include に依存する形へ整理。

確認:

* `git diff --check` OK。
* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。

### Render pass headers の Sample helper 依存を削減

目的:

* Render pass 宣言 header が `DXSampleHelper.h` に直接依存しないようにする。
* Pass desc が使う D3D12 / DirectXMath 型を各 header が明示的に include する。

変更:

* `Renderer/LightingPass.h`
* `Renderer/ShadowMaskDebugPass.h`
* `Renderer/ReflectionRayHitDebugPass.h`
* `Renderer/RayQueryShadowPass.h`
* `Renderer/RayQueryTlasDebugPass.h`
* `Renderer/SpecularDebugRayQueryPass.h`
* `Renderer/HybridReflectionPass.h`
* `Renderer/SceneGeometryPass.h`
* `Renderer/ToneMap.h`
* `Renderer/GBuffer.h`

上記から `DXSampleHelper.h` の直接 include を削除し、必要な `<d3d12.h>`, `<DirectXMath.h>`, `<cstdint>`, `<wrl/client.h>` を明示。

追加調整:

* `Renderer/GBuffer.h`
  * `ComPtr<ID3D12Resource>` を `Microsoft::WRL::ComPtr<ID3D12Resource>` に変更。

* `Renderer/GBuffer.cpp`
  * `ThrowIfFailed` を使うため、`DXSampleHelper.h` include を `.cpp` 側に明示。

確認:

* `git diff --check` OK。
* Debug x64 build 成功。
* build 時に既存 warning は残るが error は 0。
