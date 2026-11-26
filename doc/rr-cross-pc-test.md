# 別PCでの PT＋DLSS RR 安定性試験

この文書を別の Windows PC の Codex に渡し、下記の試験を順に実行してください。目的は、Work4 の RTX 3090 環境で起きた暗転・異常再起動が別PCでも再現するかを確かめることです。Work4 では短時間試験は正常終了しましたが、VSync ON の5分間試験中に BugCheck 0x133 が発生しました。

## 準備

- 比較対象は `origin/codex/rr-cross-pc-test-baseline` のコミット `2fa725d04da9044126e71b642987f915cfc0486d`。別チェックアウトで **Release x64** をビルドするか、同じコミットのビルド一式を使います。DLL・Assets・シェーダーが揃っていることを確認してください。ビルドは [Build Notes](build.md) の手順で構成を `Release`、プラットフォームを `x64` にします。Nsight は不要です。
- ビルドのコミットと EXE の SHA-256、GPU 名とドライバー版、Windows ビルド、モニターの台数と対象モニターのリフレッシュレート、HDR、G-SYNC／VRR の状態を記録します。設定を推測せず、不明なら不明と書いてください。
- 各試験で新しい出力フォルダと隔離した APPDATA を使います。同一PC内では同じウィンドウ位置と表示設定を使い、別アプリの終了、ドライバーやOS設定の変更は試験に含めません。

## 実行

リポジトリのルートから PowerShell 7 で実行します。起動関数は、既存のユーザー設定を読み込まないよう、子プロセスの APPDATA だけを切り替えます。

```powershell
$repo = $PWD.Path
$exe = Join-Path $repo 'bin/x64/Release/RtPbrSurvey.exe'
$runRoot = Join-Path $repo ('build/rr-cross-pc-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop | Out-Null

function Start-RrTrial {
    param([string]$Name, [string[]]$ExtraArgs = @())
    $folder = Join-Path $runRoot $Name
    New-Item -ItemType Directory -Path "$folder/appdata" -ErrorAction Stop | Out-Null
    $psi = [Diagnostics.ProcessStartInfo]::new($exe)
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.Environment['APPDATA'] = "$folder/appdata"
    $common = @(
        '-SceneFile', 'Assets/Scenes/WarmGlassThinShadows/scene.json',
        '-UseSceneDefaults', '-WindowSize', '1920', '1080',
        '-PathTracingReconstruction', 'rr',
        '-EnableExperimentalNativeRayReconstruction',
        '-PathTracingRrThinExperiment', '-PathTracingRrSmoothVolumeExperiment',
        '-PathTracingRrRoughVolumeExperiment', '-PathTracingRrTransmissionMotionExperiment',
        '-PathTracingRrSchedule', 'chained', '-PathTracingRrSeparateCompletionFence',
        '-PathTracingRrVerifiedPresent',
        '-RrCrashLog', "$folder/startup.log", '-LogToFile', "$folder/runtime.log"
    )
    foreach ($argument in ($common + $ExtraArgs)) {
        $psi.ArgumentList.Add($argument)
    }
    [pscustomobject]@{ Folder = $folder; Process = [Diagnostics.Process]::Start($psi) }
}

function Start-RrTimedTrial {
    param([string]$Name, [int]$Seconds, [switch]$VSyncOff)
    $trace = Join-Path (Join-Path $runRoot $Name) 'trace.json'
    $argsForTrial = @(
        '-RrFenceTrace', $trace, '-RrFenceTraceTimingOnly',
        '-RrFenceTraceCalibrateVerifiedGpu', '-RrFenceTraceWarmup', '3',
        '-RrFenceTraceSeconds', [string]$Seconds
    )
    if ($VSyncOff) {
        $argsForTrial += '-DisableVSync'
    }
    Start-RrTrial -Name $Name -ExtraArgs $argsForTrial
}
```

一段ずつ実行し、結果が確認できてから次へ進みます。異常終了、操作不能、暗転、Windows再起動が起きたらそこで止め、同じ試験を自動で再実行しないでください。

1. **VSync OFF・10秒**。 `$trial = Start-RrTimedTrial -Name 'off-10s' -Seconds 10 -VSyncOff`。自動終了を最大40秒待ちます（`$trial.Process.WaitForExit(40000)`）。終了コード0、`trace.json` が有効、`metadata.presentInterval = 0`、`freshPresents > 0`、`shutdown.drain.end` と `app.shutdown.end` を確認します。tearingの実際の有無は `metadata.presentFlags` を記録してください。
2. **VSync ON・22秒**。 `$trial = Start-RrTimedTrial -Name 'on-22s' -Seconds 22`。自動終了を最大55秒待ち、同様に確認します。期待する `presentInterval` は1、`presentFlags` は0です。両方の短時間試験で `metadata.finalRr`、`verifiedFallbackPresentations`、`metadata.endedWithPendingVerifiedRrFrame` を記録します。
3. **VSync ON・5分**。 `$trial = Start-RrTrial -Name 'on-five-minute'`。 `rr.evaluate.first.end` が出た時点から5分間、同じ静止カメラで描画を続けます。5秒ごとに実行中か、ウィンドウが応答しているかと確認時刻を `monitor.log` に記録してください。暗転は実際に画面を観測できた範囲を記録します。5分経過後に通常の閉じる操作を行い、終了コード0と両方の終了マーカーを確認します。この試験の目的は長時間の安定性で、短時間トレースの採用枚数やGPU時間と混ぜて集計しません。

## 持ち帰るレポート

出力フォルダに `report.md` を作り、環境、ビルド情報、各試験の実測時間、結果、終了コード、初回RR評価と最後のログ行をまとめてください。短時間試験ではトレースにある採用枚数・SDKエラー・フォールバック・終了時の未処理RRフレームの有無を記録します。更新レートは `freshPresents / ((windowEnd - windowStart) / qpcFrequency)` で求めます。提出からGPU開始までを計算する場合は、`verifiedTimings[].gpuClock` を成功した `calibrations` のGPU／QPC時刻で変換し、同じフレームの提出QPC（`poseQpc + poseToSubmitTicks`）を引きます。

- 異常時は、発生時刻（タイムゾーン付き）、起動からの経過時間、最後の操作、最後に確認できた応答、WindowsのBugCheckコードとダンプ名を分かる範囲で記録します。ファイル欠落、未実行、画面の未観測はそのまま明記します。
- `report.md`、`trace.json`、`startup.log`、`runtime.log`、`monitor.log` のうち生成されたものをPC内にまとめ、依頼元が比較できる形で渡します。ダンプ本体と個人設定はこのレポートに含めません。

計測項目の詳しい意味は比較用ブランチの [RRトレース仕様](https://github.com/wkawamata/RtPbrSurvey/blob/2fa725d04da9044126e71b642987f915cfc0486d/doc/rr-present-fence-trace.md) を参照してください。
