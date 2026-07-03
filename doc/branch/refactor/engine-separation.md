# DirectX12 Sample から Engine 構造へ段階的に分離する方針

目的は、Microsoft DirectX12 Sample を一気に捨てることではなく、既存の動作を保ったまま、少しずつ Engine 主導の構造へ移行することです。

重要：原則、動作は変更しない。

最終的には以下の形を目指します。

```txt
App/
 HelloEngineApp/
Engine/
 Core/
 Render/
 Scene/
 Material/
 FrameGraph/
 RayTracing/
ThirdParty/
```

現時点では、Sample 側は「Win32 起動・メインループ・ウィンドウ作成の殻」として残してよいです。
ただし、描画処理・Scene・Material・FrameGraph・RayTracing 関連は徐々に Engine 側へ移動します。

## 全体ステップ

1. 現在の Sample 固有コードと Engine 化済みコードを分類する
2. Engine の public API を整理する
3. Sample 側から直接描画処理を呼んでいる箇所を Engine 呼び出しに置き換える
4. Scene / Material / Mesh / Texture 管理を Engine 側へ移す
5. FrameGraph / GBuffer / Lighting / ToneMap を Engine 側へ閉じ込める
6. RayTracing / TLAS / BLAS / RayQuery を Engine 側の Render モジュールへ移す
7. Debug UI は App から callback で注入する形にする
8. Microsoft Sample 固有の DXSample / StepTimer / Win32Application 依存を最後に置き換える

重要なのは、各ステップごとに必ずビルド可能・起動可能な状態を保つことです。

## 最初のステップ 1

まず、現在のコードを調査して、次の3分類で整理してください。

* Sample に残すもの

 * Win32 entry point
 * Window 作成
 * Message loop
 * SwapChain 初期化に近い外側の処理

* Engine に移すべきもの

 * Device / CommandQueue / DescriptorHeap
 * FrameResource
 * GBuffer
 * FrameGraph
 * Scene
 * Material
 * glTF loading
 * Lighting
 * ToneMap
 * RayTracing 関連

* まだ判断保留のもの

 * ImGui 初期化
 * Debug UI
 * Input
 * Camera 操作
 * Sample 固有 helper

このステップでは、まだ大きな移動はしないでください。
まず、どのファイル・クラス・関数がどの分類に入るかを一覧化してください。

成果物として、`doc/refctor/engine_separation.md` を作成してください。
これは目標を管理するためのドキュメントですが、Commit単位<SHA-1>の進捗も最後に追加していきたい。

## 最初のステップ 2

次に、Engine の最小 public API を確認・整理してください。

目標は、Sample 側が最終的に次のような呼び出しだけで動く構造です。

```cpp
engine.Initialize(initDesc);
engine.SetUpdateHandler(...);
engine.SetDebugUiHandler(...);

while (running)
{
 engine.RunFrame();
}

engine.Shutdown();
```

既存コードに近い形でよいので、現在の Engine API を調査し、足りないもの・重複しているもの・Sample 側に漏れている描画責務を整理してください。

このステップでは、大規模リファクタはまだしないでください。
必要なら小さな rename やコメント追加程度に留めてください。

成果物として、`docs/engine_public_api.md` を作成してください。

## 最初のステップ 3

Sample 側の `OnRender` / `OnUpdate` / `OnDestroy` 相当の処理を確認し、Engine API 経由に寄せられる箇所を小さく置き換えてください。

方針は以下です。

* Sample 側に描画 pass の詳細を書かない
* Sample 側に GBuffer / Lighting / ToneMap の詳細を書かない
* Sample 側は Engine に camera / input / debug UI callback を渡すだけにする
* 既存の見た目が変わらないことを優先する

このステップでは、1回の変更を小さくしてください。
例えば最初は `RunFrame()` の呼び出し経路を整理するだけでよいです。

変更後は必ず以下を確認してください。

* ビルドが通る
* 起動する
* 既存シーンが表示される
* ImGui が表示される
* Resize が壊れていない
* GBuffer Debug 表示が壊れていない

## 注意点

RayTracing / Hybrid Reflection / Path Tracing に入る前に、この分離を進めたいです。

ただし、完全な独自フレームワーク化を急ぐ必要はありません。
今の目的は「Sample の形に縛られず、Engine 側に機能を追加できる状態」にすることです。

まずは Step 1 から始めて、差分を小さく出してください。

## 進捗

進捗を書きます。

## Commit単位の進捗

基本Commit単位の説明を書きます。
