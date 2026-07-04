# DXSample Split Inventory

## Summary

`DXSample` は DirectX-Graphics-Samples のアプリケーションフレームワーク基底クラスです。
現在の責務は Platform（CLI、ウィンドウサイズ、パス解決）と App（ライフサイクルフック、イベントハンドラ）が混在しています。
ただし `SampleApp` への委譲が進んでおり、多くのレンダリング関連責務は既に `HelloTextureEngine` / `GraphicsDevice` 側に移動済みです。

## Files Inspected

- `DXSample.h`, `DXSample.cpp`
- `Win32Application.h`, `Win32Application.cpp`
- `SampleApp.h`, `SampleApp.cpp`
- `D3D12HelloTexture.h`, `D3D12HelloTexture.cpp`
- `Main.cpp`
- `GraphicsDevice.h`, `GraphicsDevice.cpp`
- `Platform/`, `Shared/`

## Current Ownership Map

### DXSample の責務一覧

| Responsibility | File(s) | Lines | Current Owner | Target Owner | Notes |
|---|---|---|---|---|---|
| Window width/height | DXSample.h:65-66 | `m_width`, `m_height` → `GetWidth()`, `GetHeight()` | Mixed | Platform (WindowInfo) | 単なる整数ペア、Platform の window descriptor に含めるべき |
| Aspect ratio | DXSample.h:67 | `m_aspectRatio` | Mixed | Platform | width/height から計算可能、独立保持不要 |
| Window title | DXSample.h:82, DXSample.cpp:24 | `m_title` → `GetTitle()` | Mixed | Platform (WindowInfo) | ウィンドウ作成時にのみ使用 |
| Asset path | DXSample.h:79, DXSample.cpp:22-23 | `m_assetsPath`, `GetAssetFullPath()` | Platform | Platform (FileSystem) | `Platform/FileIO.h` と統合候補 |
| Command line parsing | DXSample.cpp:118-149 | `ParseCommandLineArgs()` | Platform | Platform (CommandLineOptions) | WARP, log path, auto-select を抽出 |
| WARP flag | DXSample.h:70, DXSample.cpp:124 | `m_useWarpDevice` | Mixed | Platform (CommandLineOptions) | CLI から設定、GraphicsDevice に渡す |
| Log file path | DXSample.h:73, DXSample.cpp:131 | `m_logFilePath` | Mixed | Platform (CommandLineOptions) | CLI から設定、SampleApp が消費 |
| FPS log interval | DXSample.h:74, DXSample.cpp:141 | `m_logFpsInterval` | Mixed | Platform (CommandLineOptions) | CLI から設定、SampleApp が消費 |
| Auto-select flag | DXSample.h:75, DXSample.cpp:147 | `m_autoSelectGltfDamagedHelmet` | Mixed | App | CLI から設定、アプリ固有の動作選択 |
| Hardware adapter enumeration | DXSample.cpp:39-98 | `GetHardwareAdapter()` | Dead code | — | 定義のみ、誰も呼び出していない。GraphicsDevice.cpp に static 版が別にある |
| Set custom window text | DXSample.cpp:101-105 | `SetCustomWindowText()` | Platform | Platform | Win32 `SetWindowText()` を呼ぶ |
| App lifecycle hooks | DXSample.h:24-27 | `OnInit()`, `OnUpdate()`, `OnRender()`, `OnDestroy()` | App (abstract) | App | SampleApp が override して実装。現在 OnUpdate/OnRender は未使用（コメントあり） |
| Event handlers | DXSample.h:30-37 | `OnKeyDown`, `OnMouseDown`, etc. | App (abstract) | App | SampleApp が override |
| `OnIdle()` | DXSample.h:37 | `OnIdle()` | App | App | Win32Application::Run のメインループで呼ばれる |

### Win32Application の責務

| Responsibility | Lines | Target Owner | Notes |
|---|---|---|---|
| Window class registration | Win32Application.cpp:33-40 | Platform | |
| Window creation | Win32Application.cpp:46-56 | Platform | `CreateWindow` 呼び出し |
| Message loop | Win32Application.cpp:64-93 | Platform | `PeekMessage` / `DispatchMessage` |
| WndProc | Win32Application.cpp:97-219 | Platform | 入力ディスパッチ、ImGui Win32 backend 統合 |
| `Run()` — lifecycle orchestration | Win32Application.cpp:24-94 | Transitional | CLI parse → OnInit → ループ → OnDestroy を順序付ける。この「起動順序」は Platform の範囲を超える |

### Circular Include

`Win32Application.h:14` → `#include "DXSample.h"` \
`DXSample.h:14` → `#include "Win32Application.h"`

循環インクルード。`#pragma once` で成立しているが、理想的な状態ではない。
DXSample.h は Win32Application.h を include する必要がない（DXSample の宣言は Win32Application 型に依存していない）。

## Dependency Notes

### 既に分離済みの責務

- `GraphicsDevice.cpp` は独自の `GetHardwareAdapter` (static) を持ち、DXSample のバージョンを呼んでいない → **DXSample::GetHardwareAdapter はデッドコード**
- D3D12 debug layer 管理は `SampleApp` (ID3D12InfoQueue) と `HelloTextureEngine` が担当
- ファイル I/O は `Platform/FileIO.h` に分離済み
- エラーハンドリングは `Shared/Error.h` に分離済み

### 継承関係のコスト

`SampleApp : public DXSample` の継承により、SampleApp は DXSample の protected メンバ (`m_useWarpDevice`, `m_logFilePath`, etc.) に直接アクセスできる。これが split を難しくしている。

### デッドコード

| Item | Reason |
|---|---|
| `DXSample::GetHardwareAdapter()` | 定義されたが誰も呼ばない |
| `DXSample::OnUpdate()` / `OnRender()` | Win32Application::Run は `OnIdle()` のみを呼ぶ。SampleApp の override はコメントで "not called" と明記 |

## Recommended First Split

**Candidate A: Extract command line options** を推奨。

理由:

1. `ParseCommandLineArgs()` は完全に自己完結している（`DXSample.cpp:118-149`）
2. 抽出後のデータは単純な value object（struct）として表現できる
3. `DXSample` のデータメンバ削減につながる
4. CLI オプションは将来的に Platform 層に所属するのが明確
5. サンプルフレームワーク由来の最も外側の依存

## Candidate Work Units

### Candidate A: コマンドラインオプションの抽出

新しいファイル:
- `Platform/CommandLineOptions.h` — `struct CommandLineOptions` を定義
- `Platform/CommandLineOptions.cpp` — パース関数

DXSample から削除:
- `ParseCommandLineArgs()` → 新しいパース関数へ移譲
- `m_useWarpDevice`, `m_logFilePath`, `m_logFpsInterval`, `m_autoSelectGltfDamagedHelmet` → CommandLineOptions のフィールドへ移動
- `m_title` への WARP 追記もパース時に処理

SampleApp 側:
- `DXSample` の protected メンバ直接参照 → `CommandLineOptions` 経由に変更

### Candidate B: 循環インクルードの解消

DXSample.h から `#include "Win32Application.h"` を削除し、前方宣言に変更。
Win32Application.h の include はそのまま（DXSample.h の完全定義が必要）。

### Candidate C: WindowInfo の抽出

`m_width`, `m_height`, `m_aspectRatio`, `m_title` を小さな value object にまとめる。
ただし、これらは SampleApp から直接参照されているため、変更範囲が広い。

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| SampleApp が DXSample の protected メンバを直接参照 | 抽出したデータを別オブジェクトにすると、SampleApp が基底クラスメンバではなく外部オブジェクトを参照する必要がある | 小さいステップで進める。まずは CLI のみ |
| `m_title` の WARP 追記 (`m_title + L" (WARP)"`) が ParseCommandLineArgs 内で行われている | 抽出後はタイトル編集の責任をどこに置くか決める必要がある | Platform の WindowInfo 作成時に適用するか、App 側で適用するか選択 |
| OnUpdate/OnRender がデッドコード | 削除するか、Win32Application のループを変更するかの判断が必要 | 今は触らない |
| DXSample の継承が split を阻害する | 抽出した値を基底クラスではなく外部から注入する形に変えると、継承関係の見直しが必要になる | 最初の抽出では基底クラス経由のアクセスを維持し、後で継承を外す |

## Verification Needed For Implementation

- Debug x64 build 成功
- `-warp`, `-LogToFile`, `-LogFPS`, `-AutoSelectGltfDamagedHelmet` が従来通り動作すること
- ウィンドウタイトルが正しく設定されること（WARP 時に "(WARP)" が追記される）
- D3D12 Debug Layer に `[ERROR]` が出ていないこと
