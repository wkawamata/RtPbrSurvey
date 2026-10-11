# Step 4 レビュー後の修正・再検証報告

更新日: 2026-10-06

追記: ユーザーの PR マージ指示により、Step 4 を c31fa7d で Commit し、MP4 対応 PR #91 を含む main を 42ce9e8 で統合した。統合後の Debug x64 ビルドと CaptureSession／Screenshot／Mp4Encoder の CTest 3件は成功。Step 5 はユーザーが実施する。以下の未コミット状態・検証結果・Git状態は2026-10-05時点の記録。
WorkingDir: `C:\work\RtPbrSurvey`
Branch: `codex/tank-capture-gui-port`
Base commit / 最終 HEAD: `9db13e45985d9dd383f574e707123d874c364dd1`

[修正依頼](C:/work/RtPbrSurvey/doc/branch/feature/tank-capture-gui-port-step4-revision-request.md) の5項目を修正し、Debug x64 ビルド、CTest 2件、終了・競合13ケース、時間同期4ケース、故障注入を確認した。差分はすべて未コミット。Commit、Push、PR、Merge、Reset、ブランチ切替は実施していない。
SceneEditor の Save／Discard／Cancel、編集拒否・Preview の反映、GUI・実マウス ROI の実操作は未検証として残る。各項目の実装・自動検証と GUI の受け入れ確認を区別する。
`C:/work/TankPhysicsSandbox` および調査用 clone は変更していない。MP4 エンコーダーは今回の対象外。

## 1. 修正項目別の結果

### 1. 終了・シーン閉鎖要求で撮影を止める

`RequestHostAction` が保留操作を記録し、`StopCaptureForPendingAction` で実行中の CaptureSession を Stop する。P 停止中でも受理済み出力の回収と形式確定を進め、その後に保留操作を一度だけ取り出して実行する。アプリ終了がシーン操作より優先し、同一優先度では最初の action と対象パス・選択シーンを保持する。
即時実行する経路にも `ResolvePendingHostAction` を使用した。ゲートを消費せず直接操作を実行すると、後続 OnIdle で同じ操作が再実行されるため、その経路を修正した。

検証コマンド: §2 の C2・C3。
C3 の `deferred-close`、`paused-close-without-stop`、`warmup-close`、`warmup-stop-close`、`draining-close`、`scene-switch-guard` が通過。P 停止中は F8 Stop を先に送らず、WM_CLOSE のみで受理1枚を保存して終了した。シーン閉鎖要求では受理2枚を保存し、100枚の上限まで撮影を続けなかった。Warmup は出力なしで解放された。すべて終了コード0。

生成物: `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-close-final/<case>/`。コマンド、各 checks、終了コード、ログ、出力ファイルは同ディレクトリの `report.json` と各ケースに保持。

### 2. SceneEditor の未保存確認をアプリ終了へ接続する

旧 SceneEditor の保留 enum を `CaptureRequestGate::PendingHostAction` に統合した。未保存確認が必要になる条件に「保留操作があること」を追加し、単に Document が Modified というだけで確認が開く不具合を修正した。
ウィンドウ終了は `OnCloseRequested` から同じ保留処理へ進む。撮影出力の解消後に未保存確認を開き、Save／Discard の判断後に終了・切替を実行する。Save が失敗した場合は保留とエラーを維持し、Cancel は保留操作を解除する。

検証コマンド: §2 の C1・C2。
Debug ビルドは成功。`TestCaptureRequestGate` は撮影待ち、確認判断待ち、終了の優先順位、重複要求、保留操作の一度だけの消費、Cancel による保留解除を確認した。
**SceneEditor の実際の Save／Discard／Cancel、Save 失敗、Cancel 後の再編集・再終了は未検証。** CTest はゲートの状態遷移を確認するもので、ImGui ダイアログの操作確認ではない。

生成物: `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-build-run1/msbuild-final.log`、`ctest-final.log`。
手動確認では未保存 Document を作成し、通常終了・撮影中終了の双方で Save／Discard／Cancel を選択する。保存先を作成不能にしたときに終了しないこと、Cancel 後に編集と再終了ができることを確認する。

### 3. Preview 再構築要求を捨てない

`m_sceneEditorPreviewRebuildPending` で要求を保持し、保存作業が解消した後に `ExecutePendingSceneEditorRebuild` が最新 Document から再構築する。SceneEditor を離れた場合には不要になった保留を解放する。
UI の編集拒否はツールバーだけでなく、Hierarchy、Transform、Material 等の編集部分にも適用した。撮影処理中または保留操作中には Document を変更する編集 UI に進まない。Undo／Redo、追加・削除も変更前に拒否する。再構築を遅延させるだけで Document と Preview の不一致を放置する経路を防いだ。
保留したシーン読込パスと選択シーンは最初の要求に固定し、実行時に選択が変わっていても別の対象を開かない。

検証コマンド: §2 の C1・C2・C3。
Debug ビルドとゲートの CTest は成功。C3 の `scene-switch-guard` でシーン閉鎖要求後の新規受理停止と保存完了を確認した。
**撮影中に Undo／Redo、追加・削除、Transform／Material を実操作し、Document と表示の一致を確認する作業は未検証。** 複数回の Preview 保留や Load 対象変更の GUI 実行確認も未実施。

生成物: 上記ビルド・CTest ログ、`C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-close-final/scene-switch-guard/`。

### 4. 自動検証の失敗を終了ステータスへ反映する

`validate_standalone_close.py` はケースごとの期待終了コード、必須 checks の存在、すべての checks の真偽、受理数と保存数、PNG の実デコードを assert する。意図した `[ERROR] Capture failed:` を GPU の ERROR／CORRUPTION と区別する。GIF は Pillow で全フレームをデコードし、受理数と一致することを確認する。EXR は OpenEXR で実デコードし、64×64 ROI と全チャンネルの有限値を確認する。
Windows 操作対象は PID と `RtPbrSurveyAppClass` の双方で絞った。時間同期スクリプトにも同じクラス条件を追加した。ログ生成前のポーリング、保存失敗ケースの blocker ファイルの準備漏れも修正した。
失敗時にも failedCase と理由を報告する。両スクリプトは既存の出力ディレクトリを作業開始前に拒否する。

検証コマンド: §2 の C3・C4・C5。
C3 は13ケースすべて通過。C5 は通常0の終了コードを1と期待させる故障注入により、`deferred-close: exit code 0 != expected 1` の AssertionError とスクリプト終了コード1を確認した。フォルダー衝突を故障注入成功と誤認しないよう、非0終了だけでなく failure の内容も照合した。
既存出力先を再指定した検証では両スクリプトが終了コード1で拒否し、既存 report.json の SHA-256 が変わらないことを確認した。

生成物: `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-close-final/`、`step4-revision-injected-failure-run2/`、`step4-revision-build-run1/injected-failure-run2.log`、`injected-exit-code-run2.txt`、`artifact-protection.json`。

### 5. 自動撮影の予約・実行中と完了・失敗を区別する

`IsAutomatedCaptureBlocking` は将来の未実行予約と in-flight を拒否条件にし、正常完了・失敗後は解除する。単発撮影の結果回収で `m_automatedCaptureCompleted` を更新する。
設定パスの有無だけを見る `HasAutomatedCapture` を削除した。自動撮影中の単発結果は OnIdle が回収し、その間 DebugUi は回収しない。完了後の GUI Screenshot は DebugUi が扱い、完了済み計画のカウンターを増やさない。これにより GUI が自動撮影結果を先に回収して終了処理を取り残すことも防ぐ。

検証コマンド: §2 の C3。
`automated-conflict`、`restart-after-automated`、`restart-after-failure`、`plan-reservation-conflict` が通過。単発自動撮影の完了後・失敗後、および2枚の計画撮影の完了後に F8 セッションを開始でき、保存と終了を確認した。計画の予約中は開始を拒否した。失敗後に再開始したケースでは、新しいPNGを保存できるが、そのプロセスの既存撮影失敗を示す終了コード1は保持する。
**GUI Start ボタンおよび完了後の単発 Capture PNG ボタンの実クリックは未検証。** 共通の判定と結果回収経路はコード確認済み。

生成物: `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-close-final/` の上記4ケース。

## 2. 実行コマンドと結果

すべて `C:\work\RtPbrSurvey` から実行した。再実行では必ず未作成の出力先を指定する。

```powershell
$python = 'C:\Users\wkawa\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'

# C1: Debug x64。最終結果: 成功、エラー0。
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe' RtPbrSurvey.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:quiet /fl '/flp:logfile=bin/CapturePort/step4-revision-build-run1/msbuild-final.log;verbosity=normal'

# C2: テストのビルドと両 CTest。結果: 2/2通過。
& $cmake --build build/scene-document-tests --config Debug --target RtPbrSurvey.CaptureSessionTests RtPbrSurvey.ScreenshotTests
& $ctest --test-dir build/scene-document-tests -C Debug -R '^RtPbrSurvey.(CaptureSession|Screenshot)$' --output-on-failure

# C3: 最終ビルドの終了・競合検証。結果: 13/13通過、終了コード0。
& $python -B Tests/CaptureSession/validate_standalone_close.py --packages bin/CapturePort/python-packages --output bin/CapturePort/step4-revision-close-final

# C4: 最終ビルドの時間同期。結果: 4/4通過、終了コード0。
& $python -B Tests/CaptureSession/validate_standalone_timing.py --output bin/CapturePort/step4-revision-timing-final

# C5: 故障注入。期待結果: AssertionError、スクリプト終了コード1。
& $python -B Tests/CaptureSession/validate_standalone_close.py --packages bin/CapturePort/python-packages --output bin/CapturePort/step4-revision-injected-failure-run2 --cases deferred-close --inject-failure
```

Debug ビルドには既存の intrin マクロ再定義と vcpkg の警告が残る。CTest の `TestPngSaveFailureForUnusableOutputPath` は既存の保存関数が失敗とエラーを返すことを確認した。保存関数は変更しておらず、その失敗をアプリの状態・ログ・終了コードへ反映した。
Pillow は既存の `bin/PathTracingValidation/python-packages`、OpenEXR 3.5.2 は検証用に `bin/CapturePort/python-packages` へ配置した。これらは検証用の未追跡・ignored 生成物で、プロジェクトの配布依存には追加していない。

### 終了・競合の最終13ケース

| ケース | アプリ終了コード | 保存・結果 | 判定 |
| --- | --- | --- | --- |
| deferred-close | 0 | 受理 1 / 保存 1 | 通過 |
| failure-exit-code | 1 | 意図した保存失敗を [ERROR] に記録 | 通過 |
| automated-conflict | 0 | PNG 1 枚 | 通過 |
| restart-after-automated | 0 | 受理 1 / 保存 1 | 通過 |
| restart-after-failure | 1 | 受理 1 / 保存 1 | 通過 |
| plan-reservation-conflict | 0 | PNG 2 枚 | 通過 |
| paused-close-without-stop | 0 | 受理 1 / 保存 1 | 通過 |
| warmup-close | 0 | 受理 0 / 保存 0 | 通過 |
| warmup-stop-close | 0 | 受理 0 / 保存 0 | 通過 |
| draining-close | 0 | 受理 2 / 保存 2 | 通過 |
| scene-switch-guard | 0 | 受理 2 / 保存 2 | 通過 |
| gif-finalize | 0 | 受理 1 / デコード 1 フレーム | 通過 |
| exr-finalize | 0 | 受理 1 / 保存 1 | 通過 |

全ケースで D3D12 ERROR／CORRUPTION は0。正常ケースの保存失敗は0。失敗を意図した2ケースではアプリの保存失敗ログを確認した。既存の D3D12 WARNING はあるため、警告0という判定はしていない。
GIF・EXR のケースは終了要求までに受理した1フレームを実デコードした。EXR の複数フレーム連番・動くシーン・全描画方式の確認は Step 5 に残す。

### 時間同期の最終4ケース

固定60 FPSの撮影時刻は 0.066666667 / 0.083333333 / 0.100000000 秒、描画フレーム3 / 6 / 9。
固定30 FPSは 0.133333333 / 0.166666667 / 0.200000000 秒、描画フレーム3 / 6 / 9。
実時間30 FPSはシミュレーション時計を使用しない。P／F／F8ケースではP停止後、Fで一度だけ1/60秒進み、F8停止後に保存して正常終了した。
合計11枚を保存し、全プロセス終了コード0、D3D12 ERROR／CORRUPTION 0。

`report.json` に基点、dirty 状態、ソースと実行ファイルの SHA-256 を保持した。終了・競合と時間同期の最終実行ファイルは同一ハッシュ。全件実行後に両スクリプトの出力ディレクトリ作成を `exist_ok=False` にした。追加変更は出力先の保護だけで、既存出力先の拒否と故障注入で別途検証した。全件実行報告のスクリプトハッシュは、この保護を追加する前の実行時のもの。

## 3. 生成物と作業中の検証失敗

最終結果:

- `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-build-run1/`: 最終ビルド、CTest、故障注入・既存出力先の保護ログ。
- `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-close-final/`: 13ケースの結果、ログ、PNG／GIF／EXR。
- `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-timing-final/`: 時間同期4ケースの結果、ログ、PNG。
- `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-injected-failure-run2/`: 故障注入の失敗理由と、正常に保存された1枚のPNG。

途中経過も残した。`step4-revision-close-run1` は3ケース通過後、ログ生成前の読み取りで停止した。`step4-revision-close-run2` は再開始1ケース通過後、失敗ケースの準備漏れで停止した。準備を修正した `step4-revision-close-run3` は残る9ケースが通過した。`step4-revision-timing-run1` は4ケース通過。これらは最終結果と区別する。

**既存生成物への変更事故:** 最初の故障注入で既存の `C:/work/RtPbrSurvey/bin/CapturePort/step4-revision-injected-failure/` と出力先が重複し、ケース作成の FileExistsError を報告する際、そのフォルダーの旧 `report.json` を上書きした。旧PNG・d3d12.logには書き込んでいない。旧JSONは復元できていないため、旧実行の証拠として扱わない。この実行は故障注入成功には数えない。以後は出力先全体の既存チェックを追加し、未作成の `step4-revision-injected-failure-run2` で期待する AssertionError を確認した。以前の Step 2・Step 3・step4-validation-run1..run4・step4-timing-rerun は変更していない。

## 4. 未検証・残作業

- SceneEditor の Save／Discard／Cancel、保存失敗、Cancel 後の再編集・再終了の実操作。
- 撮影中の編集拒否、最新 Document からの Preview 再構築、複数要求、Load対象保持の GUI 実行確認。
- GUI配置、Start／Stop／Capture PNG のクリック、実マウス ROI の目視確認。
- 動くシーン、全描画方式、EXR連番、GUIスライダーと保存中出力の関係。
- `ReloadEnvironmentResources` の撮影中再構築は今回の保護対象外。共通環境リソースの変更との回帰は Step 5 に残す。
- MP4 は共通対応 PR マージ後に取り込む。

この環境では上記の GUI 実操作を行っていない。ゲートの CTest と実アプリへのキーメッセージによる検証を、SceneEditor やマウス操作の受け入れ確認に置き換えていない。計画書にも未確認項目を残した。

## 5. 最終 Git 状態

```text
 M App/DebugUi.cpp
 M App/RtPbrSurveyApp.cpp
 M App/RtPbrSurveyApp.h
 M App/SceneEditorUi.cpp
 M CMakeLists.txt
 M Platform/IApplication.h
 M Platform/Win32Application.cpp
 M RtPbrSurvey.vcxproj
 M Runtime/CaptureSessionUi.cpp
 M Runtime/CaptureSessionUi.h
 M Runtime/SceneRenderer.cpp
 M Runtime/SceneRenderer.h
 M Tests/CaptureSession/validate_standalone_timing.py
 M Tests/CaptureSessionTests.cpp
 M Tests/ScreenshotTests.cpp
 M doc/branch/feature/tank-capture-gui-port-plan.md
?? Runtime/CaptureRequestGate.cpp
?? Runtime/CaptureRequestGate.h
?? Tests/CaptureSession/validate_standalone_close.py
?? doc/branch/feature/tank-capture-gui-port-step4-report.md
?? doc/branch/feature/tank-capture-gui-port-step4-request.md
?? doc/branch/feature/tank-capture-gui-port-step4-revision-request.md
```

HEAD は基点のまま。`git diff --check` は通過。変更テキストは CRLF・UTF-8 BOMなしを確認。生成物は ignored の bin/build 内にあり、stageしていない。
Commit、Push、PR、Merge、Reset、ブランチ切替は行っていない。
本報告の完了範囲は修正実装、自動再検証、報告・計画の更新。§4のGUI受け入れ項目は未検証として残る。

Status: done
