# DXSampleHelper.h Refactoring Complete Report

Branch: `refactor/engine-separation`
Date: 2026-07-04

## Summary

`DXSampleHelper.h` は DirectX-Graphics-Samples 由来の便利関数・マクロが混在した catch-all header でした。
これを責務ごとに 5 つの owner header へ分割し、全 consumer を直接 owner header に置き換えました。

現在 `DXSampleHelper.h` は削除済みです。

## Commit History

| Commit | Date | Description |
|--------|------|-------------|
| `4c1eb1f002` | earlier | **Platform/FileIO.h 抽出** — `GetAssetsPath`, `ReadDataFromFile`, `ReadDataFromDDSFile` を `Platform/FileIO.h` へ移動 |
| `81ae3bd0d3` | earlier | **Shared/Error.h 抽出** — `HrToString`, `HrException`, `SAFE_RELEASE`, `ThrowIfFailed` を `Shared/Error.h` へ移動 |
| `33efbb50d2` | earlier | **Shared/D3d12DebugName.h, Shared/D3d12Helpers.h, Platform/ShaderCompiler.h 抽出** — `SetName`, `SetNameIndexed`, NAME マクロ, `CalculateConstantBufferByteSize`, `ResetComPtrArray`, `ResetUniquePtrArray`, `CompileShader` をそれぞれの header へ移動 |
| `60c06c7e19` | earlier | **Renderer/*.cpp の include 置換** — 9 ファイルの `DXSampleHelper.h` include を `Shared/Error.h`, `Platform/FileIO.h`, `<wrl/client.h>` に置き換え |
| `b1cc57f3c4` | 2026-07-04 | **全ソースファイルから DXSampleHelper.h include 完全除去** — 残り 9 ファイル (`WorkMeter.h`, `GraphicsDevice.h/.cpp`, `MyDx12Utils.h`, `DXSample.h/.cpp`, `D3D12HelloTexture.h/.cpp`) を置き換え |

## Created Headers (5 files)

| File | Contents | Category |
|------|----------|----------|
| `Platform/FileIO.h` | `GetAssetsPath`, `ReadDataFromFile`, `ReadDataFromDDSFile` | Platform file I/O |
| `Platform/ShaderCompiler.h` | `CompileShader` | Platform shader compilation |
| `Shared/Error.h` | `HrToString`, `HrException`, `SAFE_RELEASE`, `ThrowIfFailed` | Shared error handling |
| `Shared/D3d12DebugName.h` | `SetName`, `SetNameIndexed`, `NAME_D3D12_OBJECT`, `NAME_D3D12_OBJECT_INDEXED` | D3D12 debug naming |
| `Shared/D3d12Helpers.h` | `CalculateConstantBufferByteSize`, `ResetComPtrArray`, `ResetUniquePtrArray` | D3D12 utility helpers |

## Current State of DXSampleHelper.h

```cpp
#pragma once
#include "Platform/FileIO.h"
#include "Shared/Error.h"
#include "Shared/D3d12DebugName.h"
#include "Shared/D3d12Helpers.h"
#include "Platform/ShaderCompiler.h"

using Microsoft::WRL::ComPtr;
```

- Include 元のソースファイル: **0 件**
- 実質的に `using Microsoft::WRL::ComPtr;` だけの互換性 shim

## Files Modified (cumulative)

Total: ~28 files touched across all commits:

- `DXSampleHelper.h` — 元の全関数を削除し 5 つの `#include` に置き換え
- `DXSample.h`, `DXSample.cpp` — include 置換
- `D3D12HelloTexture.h`, `D3D12HelloTexture.cpp` — include 置換
- `GraphicsDevice.h`, `GraphicsDevice.cpp` — include 置換
- `MyDx12Utils.h` — include 置換
- `WorkMeter.h` — include 置換
- `Renderer/*.cpp` (9 files) — include 置換
- `RtPbrSurvey.vcxproj`, `.filters` — 新ファイル登録
- `doc/branch/refactor/engine-separation.md` — 各 commit の記録

## Verification

- `DXSampleHelper.h` include: **0 matches** across all `.h` / `.cpp`
- Debug x64 build: **0 errors**
- D3D12 Debug Layer: `[ERROR]` / `[WARNING]` **none** after ~600 frames
- All 6 moved functions were **dead code** (zero callers outside DXSampleHelper.h)

## Remaining Candidates

| Priority | Item | Description |
|----------|------|-------------|
| Done | `DXSampleHelper.h` deleted | No longer needed — 0 consumers, 0 includes |
| Medium | Candidate 3 | Extract App debug UI (`SampleApp::DrawDebugUi` → `App/`) |
| Medium | Candidate 4 | Define `Engine/FrameGraph` boundary (`Renderer/RenderPass*.h`) |
| Medium | Step 2 | App/Platform interface definition (DXSample splitting) |
