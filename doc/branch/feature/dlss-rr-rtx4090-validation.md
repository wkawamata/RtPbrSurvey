# DLSS Ray Reconstruction RTX 4090 Validation

この手順は RTX 4090 環境で `codex/dlss-ray-reconstruction` の DLSS Ray Reconstruction native evaluate 経路を検証するためのものです。

対象:

- Branch: `codex/dlss-ray-reconstruction`
- Minimum commit: `f22e9fc Log raw ray reconstruction Streamline results`
- Workspace example: `C:\work\RtPbrSurvey-work-2`

## 注意

- `Screenshots/`、`rr_*.log`、`bin/`、`obj/`、`.vs/`、生成物は commit しないでください。
- 4090 側の結果は `doc/branch/feature/dlss-rr-rtx4090-validation-result.md` に記入してください。
- PNG やログ本体は commit せず、必要なログ行と画像所見だけを結果 Markdown に転記してください。
- `-EnableExperimentalNativeRayReconstruction` は `-EnableDlssRayReconstruction` を暗黙に含みますが、検証コマンドでは状態を明確にするため両方指定します。
- CLI flags は実行時 settings override です。scene config は書き換えません。
- 4090 実機結果を確認するまでは PR 作成、main merge、history rewrite、rebase、reset は行わないでください。

## Branch Setup

```powershell
Set-Location C:\work\RtPbrSurvey-work-2
git fetch origin
git switch codex/dlss-ray-reconstruction
git pull --ff-only
git rev-parse --short HEAD
git log -1 --oneline
git status --short
```

確認:

- `git log -1 --oneline` が `f22e9fc Log raw ray reconstruction Streamline results` 以降であること。
- `git status --short` に未コミットのソース変更がないこと。
- 未追跡の `Screenshots/` や `rr_*.log` があっても commit しないこと。

## Build

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

期待:

- Build succeeded.
- 既知の vcpkg import warning は許容。
- C++ compile/link error がないこと。

## Runtime DLL Check

Debug 実行ディレクトリに Streamline / NGX DLL が配置されているか確認します。

```powershell
Get-ChildItem -LiteralPath .\bin\x64\Debug -Filter "sl*.dll" | Select-Object Name,Length
Get-ChildItem -LiteralPath .\bin\x64\Debug -Filter "nvngx_dlss*.dll" | Select-Object Name,Length
```

最低限確認したい DLL:

- `sl.interposer.dll`
- `sl.common.dll`
- `sl.dlss.dll`
- `sl.dlss_d.dll`
- `nvngx_dlss.dll`

見つからない DLL がある場合は、Streamline SDK / NGX runtime の配置を先に直してから runtime test を行ってください。

## Copy Fallback Test

RR render-graph path を有効化し、native evaluate は無効のまま capture します。

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_copy_fallback_4090.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_copy_fallback_4090.log
```

ログ抽出:

```powershell
Select-String -LiteralPath rr_copy_fallback_4090.log -Pattern "\[RR\]|\[ERROR\]|\[WARNING\]|D3D12|Device"
```

期待:

- Process exit code 0。
- `Screenshots\rr_copy_fallback_4090.png` が生成される。
- `support=available`。
- native evaluate off のため、native output ではなく fallback になる。
- D3D12 `ERROR` がない。
- device removed / crash がない。

## Native RR Test

RR render-graph path と guarded native evaluate を有効化して capture します。

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -AutoSelectGltfDamagedHelmet -EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction -CaptureReflectionResolvedRadiance -CapturePath Screenshots\rr_native_4090.png -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile rr_native_4090.log
```

ログ抽出:

```powershell
Select-String -LiteralPath rr_native_4090.log -Pattern "\[RR\]|\[ERROR\]|\[WARNING\]|D3D12|Device"
```

期待:

- Process exit code 0。
- `Screenshots\rr_native_4090.png` が生成される。
- `support=available`。
- `supportQueryResult=Result::eOk`。
- `inputReadiness=ready`。
- `lastEvaluate=available`。
- `lastEvaluateStatus=Available`。
- `lastEvaluateResult=Result::eOk`。
- `lastEvaluateOutput=native-output`。
- D3D12 `ERROR` がない。
- device removed / crash がない。
- capture が完全な黒、NaN 的な破綻、明白な全面ノイズになっていない。

## Optional Capture Sanity Check

PNG が生成され、完全な黒ではないことを簡易確認します。

```powershell
Add-Type -AssemblyName System.Drawing
$paths = @("Screenshots\rr_copy_fallback_4090.png", "Screenshots\rr_native_4090.png")
foreach ($path in $paths) {
    $bitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $path))
    $width = $bitmap.Width
    $height = $bitmap.Height
    $sampleCount = 0
    $nonBlack = 0
    $sum = 0
    for ($y = 0; $y -lt $height; $y += [Math]::Max(1, [int]($height / 32))) {
        for ($x = 0; $x -lt $width; $x += [Math]::Max(1, [int]($width / 32))) {
            $c = $bitmap.GetPixel($x, $y)
            $luma = 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B
            $sum += $luma
            if ($c.R -ne 0 -or $c.G -ne 0 -or $c.B -ne 0) { $nonBlack++ }
            $sampleCount++
        }
    }
    $bitmap.Dispose()
    $mean = $sum / [Math]::Max(1, $sampleCount)
    Write-Output "$path width=$width height=$height samples=$sampleCount nonBlack=$nonBlack meanLuma=$([Math]::Round($mean, 2))"
}
```

## Report Template

4090 実機テスト後、この形式で `doc/branch/feature/dlss-rr-rtx4090-validation-result.md` を作成してください。
GPU 名、driver version、tested commit、build result、Copy Fallback 結果、Native RR 結果、raw Streamline results、D3D12 errors、capture 所見を記録します。
PNG や `rr_*.log` 本体は commit せず、必要なログ行と画像所見だけを Markdown へ転記してください。
4090 側では結果 Markdown だけを commit します。

推奨 commit message:

```text
Record RTX 4090 ray reconstruction validation
```

結果ファイル:

```text
doc/branch/feature/dlss-rr-rtx4090-validation-result.md
```

```text
GPU:
Driver version:
OS:
Branch:
Tested commit:

DLL check:
- sl.interposer.dll:
- sl.common.dll:
- sl.dlss.dll:
- sl.dlss_d.dll:
- nvngx_dlss.dll:

Build:
- Debug x64 MSBuild:

Copy fallback command:
<command>

Copy fallback result:
- exit code:
- capture path:
- capture generated:
- support:
- status:
- supportQueryResult:
- inputReadiness:
- inputReason:
- lastEvaluate:
- lastEvaluateStatus:
- lastEvaluateResult:
- lastEvaluateOutput:
- D3D12 ERROR:
- D3D12 WARNING:
- device removed/crash:
- visual/capture notes:

Native RR command:
<command>

Native RR result:
- exit code:
- capture path:
- capture generated:
- support:
- status:
- supportQueryResult:
- inputReadiness:
- inputReason:
- lastEvaluate:
- lastEvaluateStatus:
- lastEvaluateResult:
- lastEvaluateOutput:
- D3D12 ERROR:
- D3D12 WARNING:
- device removed/crash:
- visual/capture notes:

Conclusion:
```
