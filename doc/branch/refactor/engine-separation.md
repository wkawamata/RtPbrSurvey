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

## 進捗

進捗を書きます。

## Commit 単位の進捗

基本 Commit 単位の説明を書きます。
