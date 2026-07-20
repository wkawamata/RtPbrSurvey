# DLSS Runtime Fixes

Working checkout: `C:\work\RtPbrSurvey-work-3`

Branch: `codex/dlss-runtime-validation`

Commit: `ab690fa`

## Summary

DLSS が DLAA モードで正常に動作した後、非 DLAA モード（Ultra Quality 等）での出力の破損と、DLAA→非 DLAA 切替時のクラッシュを修正。

## 修正内容

### 1. ビューポート／シザーレクトの不一致修正

**問題**: `BeginFrame()` で設定されるビューポートは常に出力解像度（例: 1920x1080）だが、GBuffer・深度・ライティング等のレンダリングターゲットは非 DLAA モードでレンダリング解像度（例: Ultra Quality = 1418x798）。NDC→スクリーン変換が正しく行われず、シーンの一部だけが描画され、黒背景に投影される。

**修正**:
- `RtPbrSurveyEngine.h` に `m_renderViewport` / `m_renderScissorRect`（レンダリング解像度）を追加
- `BeginFrame()`: レンダリング解像度のビューポートを使用
- `ExecuteToneMapPass()`: 出力解像度のビューポートを復元
- `ApplyResize()`: 両方のビューポートを更新

### 2. Streamline 評価後のコマンドリスト状態復元

**問題**: DLSS Programming Guide Section 7.0 により、`slEvaluateFeature` 後にホストがコマンドリストの状態を復元する必要がある。descriptor heap のみ復元していた。

**修正**:
- デスクリプタヒープの復元（既存）
- グラフィクスルートシグネチャの復元（新規）
- パイプラインステートの復元（新規）
- リソーストラッキング状態の同期（新規）

### 3. デスクリプタヒープリーク防止

**問題**: `CreateReflectionRayHit/Color/Material/EmissionDescriptors()` が毎回 `AllocWithHandle()` を呼び、`ApplyResize()` のたびにヒープを浪費。

**修正**: `Index == UINT_MAX` ガードを追加し、未確保時のみ確保。

## 検証状況

| モード | 状態 |
|--------|------|
| DLAA | OK（D3D12 エラーなし、`-AutoSelectGltfDamagedHelmet` で確認） |
| 非 DLAA（Ultra Quality 等） | 修正済み、**要手動テスト** |
| DLAA→Ultra Quality 切替 | 修正済み、**要手動テスト** |

## Review follow-ups (dlss-runtime-review-fixes)

### First-frame render viewport initialization

**問題**: `Initialize()` では `m_viewport` / `m_scissorRect`（出力解像度）のみ初期化し、`m_renderViewport` / `m_renderScissorRect`（レンダリング解像度）は未初期化のまま。最初の `BeginFrame()` が `ApplyResize()` より先に実行されると、未初期化のレンダリング解像度ビューポートが GPU に送信される。

**修正**: `Initialize()` 内の `UpdateRenderDimensions()` 直後に `m_renderViewport` / `m_renderScissorRect` を `m_renderWidth` / `m_renderHeight` で初期化。

### Dead helper removal

`HasTemporalUpscalerPassOutput()` は呼出し元が0件のため、宣言（`.h:990`）と定義（`.cpp:330`）を削除。

### Streamline projectId

`Renderer/StreamlineAdapter.cpp:158` の `projectId = "a0f57b54-1daf-4934-90ae-c4035c19df04"` は RtPbrSurvey の NGX 初期化に意図的に固定されている。この値は Streamline ランタイムから DLSS プラグインをロードするために必要であり、テストや開発段階でのみ使用される。

将来のプラグイン/ビルドの柔軟性（`projectId` の外部化や設定ファイル化）は本タスクのスコープ外であり、後続タスクのフォローアップとして残す。現在の動作を確認済みの ID を変更しない。

## 既知の課題

- ジッタ（`jitterOffset`）は `{0, 0}` で固定。将来実装必要。
- 非 DLAA モードの検証は UI での手動切替が必要。
