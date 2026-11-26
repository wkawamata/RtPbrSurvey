# PT + DLSS RR 安定性試験

## 結果概要

| 試験 | 結果 | 計測時間 | 新規 RR 採用 | 採用レート |
| --- | --- | ---: | ---: | ---: |
| VSync OFF・10秒 | 正常終了 | 10.006 秒 | 537 | 53.665 fps |
| VSync ON・22秒 | 正常終了 | 22.020 秒 | 658 | 29.883 fps |
| VSync ON・5分 | 正常終了 | 300.018 秒 | 対象外 | 対象外 |

全試験で終了コード 0 と両方の終了マーカーを確認。5分試験では開始時と5秒ごとの計61回すべてで同じウィンドウが応答し、通常のウィンドウ終了操作で閉じた。試験中の Windows 再起動は発生せず、試験開始以降のシステムイベント ID 41、1001、4101 に該当記録なし。暗転は画面未観測のため判定できない。

全試験の対象画面は主画面の LG Ultra HD（DISPLAY1、Windows 報告 29 Hz、HDR オフ）。VSync OFF の `presentFlags=512` は `DXGI_PRESENT_ALLOW_TEARING`。画面上のティアリングは未観測。


## 環境とビルド

- 比較対象: `origin/codex/rr-cross-pc-test-baseline` の `2fa725d04da9044126e71b642987f915cfc0486d`。別チェックアウトの HEAD で照合。
- 構成: Release x64。Visual Studio 2022 Professional の MSBuild 17.14.60。
- NVIDIA Streamline SDK 2.12.0。公式 ZIP の SHA-256 は手順書記載値と一致。
- GPU: NVIDIA GeForce RTX 4090、ドライバー 596.49（WDDM 3.1）。
- Windows: 23H2、ビルド 22631.7711。
- ディスプレイ: dxdiag に現行モード付きで 3 台。LG Ultra HD 29 Hz（DISPLAY1、主画面）、ROG PG27U 60 Hz（DISPLAY2）、LG HDR 4K（DisplayLink）30 Hz（DISPLAY5）。解像度はいずれも 3840×2160。
- HDR: ROG PG27U は HDR オン、LG Ultra HD はオフ、DisplayLink 側は非対応（dxdiag の AdvancedColorEnabled と色空間で確認）。G-SYNC／VRR は不明。
- 画面取得機能に接続できず、画面の直接観測は未実施。暗転の有無は未観測として扱う。ウィンドウの存続と応答をプロセスから記録する。
- 試験間で同じ起動引数のウィンドウ寸法（1920×1080）を使い、位置や表示設定は操作しない。APPDATA は試験ごとに隔離。
- 試験準備時、既存チェックアウトの RtPbrSurvey（PID 10276）が NVIDIA のプロセス一覧に存在。通常ウィンドウは取得できず、CPU 時間の増加は約 1 分で 0.02 秒。既存プロセスの停止や設定変更は行わない。
- ビルド成功（終了コード 0、警告 MSB4011）。EXE SHA-256: 443D8FD7F8FD503BFCF5BF558E6DCE6C5C013732B1B4F94390235A18083EF899. Streamline DLL 6 個、D3D12/DXC/PIX DLL、シーン参照ファイル、コンパイル済みシェーダー 50 個を確認。

## off-10s

- 開始: 2026-11-26T08:26:21.6707334+09:00
- 対象ディスプレイ: \\.\DISPLAY1、主画面=True、表示領域={X=0,Y=0,Width=3840,Height=2160}。
- 起動から判定まで: 26.893 秒。終了コード: 0。
- 初回 RR 評価: 2026-11-25T23:26:35.420Z 22 rr.evaluate.first.end 0 0 0
- 終了マーカー: shutdown.drain.end=True、app.shutdown.end=True
- startup.log 最終行: 2026-11-25T23:26:48.360Z 41 app.shutdown.end 0 0 0
- runtime.log 最終行: [RR] support=available status=Available supportQueryResult=Result::eOk inputReadiness=ready inputReason=Ready lastEvaluate=available lastEvaluateStatus=Available lastEvaluateResult=Result::eOk lastEvaluateOutput=native-output adapter="NVIDIA GeForce RTX 4090" vendor=0x10DE device=0x2684 luid=00000000:00051806
- トレース計測区間: 10.006 秒、新規 RR 採用: 537、採用レート: 53.665 fps。
- presentInterval=0、presentFlags=512
- finalRr: {"cancelled":0,"completed":654,"inputRejected":0,"outputRejected":0,"resets":1,"sdkFailures":0}
- verifiedFallbackPresentations=0、endedWithPendingVerifiedRrFrame=False
- 結果: ログ、終了コード、適用されるトレース条件を満たした。

## on-22s

- 開始: 2026-11-26T08:27:01.3477589+09:00
- 対象ディスプレイ: \\.\DISPLAY1、主画面=True、表示領域={X=0,Y=0,Width=3840,Height=2160}。
- 起動から判定まで: 33.331 秒。終了コード: 0。
- 初回 RR 評価: 2026-11-25T23:27:09.443Z 22 rr.evaluate.first.end 0 0 0
- 終了マーカー: shutdown.drain.end=True、app.shutdown.end=True
- startup.log 最終行: 2026-11-25T23:27:34.483Z 41 app.shutdown.end 0 0 0
- runtime.log 最終行: [RR] support=available status=Available supportQueryResult=Result::eOk inputReadiness=ready inputReason=Ready lastEvaluate=available lastEvaluateStatus=Available lastEvaluateResult=Result::eOk lastEvaluateOutput=native-output adapter="NVIDIA GeForce RTX 4090" vendor=0x10DE device=0x2684 luid=00000000:00051806
- トレース計測区間: 22.020 秒、新規 RR 採用: 658、採用レート: 29.883 fps。
- presentInterval=1、presentFlags=0
- finalRr: {"cancelled":0,"completed":739,"inputRejected":0,"outputRejected":0,"resets":1,"sdkFailures":0}
- verifiedFallbackPresentations=0、endedWithPendingVerifiedRrFrame=False
- 結果: ログ、終了コード、適用されるトレース条件を満たした。

## on-five-minute

- 開始: 2026-11-26T08:27:46.4751881+09:00
- 対象ディスプレイ: \\.\DISPLAY1、主画面=True、表示領域={X=0,Y=0,Width=3840,Height=2160}。
- 起動から判定まで: 308.525 秒。終了コード: 0。
- 初回 RR 評価マーカーの検出から通常の閉じる操作まで: 300.018 秒。
- 初回 RR 評価: 2026-11-25T23:27:54.441Z 22 rr.evaluate.first.end 0 0 0
- 終了マーカー: shutdown.drain.end=True、app.shutdown.end=True
- startup.log 最終行: 2026-11-25T23:32:54.862Z 40 app.shutdown.end 0 0 0
- runtime.log 最終行: [RR] support=available status=Available supportQueryResult=Result::eOk inputReadiness=ready inputReason=Ready lastEvaluate=available lastEvaluateStatus=Available lastEvaluateResult=Result::eOk lastEvaluateOutput=native-output adapter="NVIDIA GeForce RTX 4090" vendor=0x10DE device=0x2684 luid=00000000:00051806
- 結果: ログ、終了コード、適用されるトレース条件を満たした。

## Git クローン後に Visual Studio 2022 で DLSS を有効にする

NVIDIA Streamline SDK 2.12.0 は Git リポジトリの外から取得する。SDK を配置してからソリューションをビルドする。

1. [NVIDIA の公式リリース](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.12.0)から `streamline-sdk-v2.12.0.zip` を取得する。PowerShell で `Get-FileHash -Algorithm SHA256 .\streamline-sdk-v2.12.0.zip` を実行し、`f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48` と照合する。
2. クローンしたリポジトリの `third_party\streamline-sdk-2.12.0\` に展開する。その直下に `include`、`lib`、`bin` があり、`include\sl.h` が見える配置にする。
3. Visual Studio 2022 で `RtPbrSurvey.sln` を開き、`Debug | x64` または `Release | x64` をビルドする。SDK の配置前にビルド済みなら同じ構成でリビルドする。MSBuild が SDK を検出し、`sl.interposer.dll`、`sl.common.dll`、`sl.dlss.dll`、`sl.dlss_d.dll`、`nvngx_dlss.dll`、`nvngx_dlssd.dll` を EXE の隣にコピーする。
4. 起動後、DLSS の `Temporal Upscaler` が `Available` になっていることを確認する。DLSS Super Resolution を使う場合は、UI の `DLSS Enabled` をオンにする。

今回の **PT＋DLSS Ray Reconstruction** を Visual Studio から確認する場合は、`Release | x64` を選択し、プロジェクトの **プロパティ → デバッグ → コマンド引数** に `-PathTracingReconstruction rr -EnableExperimentalNativeRayReconstruction` を設定する。今回と同じシーンや実験設定まで揃える場合は、[試験手順書](https://github.com/wkawamata/RtPbrSurvey/blob/1a375fb93960f63572262bac8dd9479692a93f97/doc/rr-cross-pc-test.md) の `$common` にある起動引数を使う。ログの `lastEvaluateResult=Result::eOk` と `lastEvaluateOutput=native-output` でネイティブ RR の動作を確認できる。

## Visual Studio の Debug 起動で発生した RR フォールバック

手動確認では `PT + RR`、`RR Enabled`、`Experimental Native Evaluate` が選択され、SDK のサポートは `Available` だった。一方、`Reconstruction output: native (incompatible-inputs)`、`RR completed: 0`、`RR SDK: -- (not executed)` が表示され、通常の Path Tracing 出力にフォールバックしていた。

その時点のプロセスは試験用チェックアウトの `Debug | x64` の EXE だった。当初の Visual Studio 起動引数は `Release | x64` だけに設定されていたため、Debug には試験と同じシーン・設定を渡す起動引数がなかった。ガラス材を含む試験シーンでは、薄いガラスと該当する体積ガラスを RR に通すための実験フラグが必要で、これらがオフだと RR の入力適合判定でフォールバックする。手動起動時の対象シーンと各フラグの値はログ未生成のため未確認。

試験用チェックアウトのローカル設定 `RtPbrSurvey.vcxproj.user` に `Debug | x64` の起動構成を追加し、以下の既存オプションを設定した。

| オプション | 用途 |
| --- | --- |
| `-SceneFile Assets/Scenes/WarmGlassThinShadows/scene.json -UseSceneDefaults -WindowSize 1920 1080` | 試験シーンとシーン既定値を読み込み、同じウィンドウ寸法で開く。 |
| `-PathTracingReconstruction rr -EnableExperimentalNativeRayReconstruction` | PT の再構成に RR を選び、ネイティブ DLSS RR の評価を許可する。 |
| `-PathTracingRrThinExperiment -PathTracingRrSmoothVolumeExperiment -PathTracingRrRoughVolumeExperiment` | 薄いガラス、滑らかな体積ガラス、条件を満たす粗い体積ガラスを RR の対象にする。 |
| `-PathTracingRrTransmissionMotionExperiment` | 透過材があるシーンで、カメラや物体の動きに対する RR 履歴の維持を有効にする。 |
| `-PathTracingRrSchedule chained -PathTracingRrSeparateCompletionFence -PathTracingRrVerifiedPresent` | RR の処理順序、完了フェンス、表示前の検証を試験条件と揃える。 |
| `-RrCrashLog build/rr-vs-manual-debug/startup.log -LogToFile build/rr-vs-manual-debug/runtime.log` | Debug の起動・RR 診断ログを出力する。 |

Debug の `APPDATA` は `build/rr-vs-manual-debug/appdata` に隔離した。MSBuild で `Debug | x64` と `Release | x64` の SDK 検出と起動引数の読み込みを確認済み。変更後の Visual Studio からの実起動は未確認。

動作確認では、試験用 SLN を開き直し、`Debug | x64` を選んで F5 で起動する。描画開始後、`Reconstruction Details` で `RR completed` が増え、`Reconstruction output` が `rr` になることを確認する。ログで確認する場合は `build/rr-vs-manual-debug/runtime.log` の `lastEvaluateResult=Result::eOk` と `lastEvaluateOutput=native-output` を照合する。
