# TankPhysics Capture GUI Port: Step 5 引継ぎ

更新日: 2026-10-08 (JST)
リポジトリ: `C:\work\RtPbrSurvey`
作業ブランチ: `codex/capture-gui-step5`
取り込み先: `main`
関連PR: #91 (共通MP4対応), #92 (Step 4、マージ済み)

## 目的と現在地

TankPhysicsの撮影GUI機能をRtPbrSurveyへ移植する作業のStep 5を引き継ぐ。
PNG/EXR/GIF/MP4のGPU出力、GUIの基本操作、Mouse ROI、空のSceneEditor Documentの扱いを確認した。
検証中に見つけた不具合を修正し、作業と制限事項を報告書へ記録している。
Step 5は未完了。未確認項目はこの文書末尾にある。

今回の変更を含むPRをmainへマージした後、作業を続ける場合はmainの最新状態から新しい`codex/`ブランチを作成する。
PRのCI結果、マージコミット、リモートブランチの削除状態を確認してから次の実装に入る。

## 変更内容

- `Engine/RtPbrSurveyEngine.cpp`: 空Mesh/頂点0のSceneEditor PreviewでGPUジオメトリ生成を飛ばし、空Documentを維持する。
- `App/SceneEditorUi.cpp`: 終了時Save and Continueの失敗理由をモーダル表示中に維持する。
- `App/RtPbrSurveyApp.cpp`: Capture Sessionの実行時保存失敗を終了コード1に加えて`[ERROR]`としてLogToFileへ記録する。
- `Tests/CaptureSession/validate_standalone_formats.py`: PNG/EXR/GIF/MP4のGPU出力をデコードし、サイズ、フレーム数、時刻、保存結果を確認する。
- `Tests/CaptureSession/validate_standalone_save_failures.py`: PNG/EXR/GIF/MP4の保存失敗、ログ、終了コードを実アプリで確認する。
- `doc/branch/feature/tank-capture-gui-port-plan.md`と`doc/branch/feature/tank-capture-gui-port-step5-report.md`: 進捗、結果、未検証項目を記録する。

## 検証済み

- Debug x64 MSBuild成功。既存のMSBuild/macro警告あり。
- `RtPbrSurvey.Screenshot`、`RtPbrSurvey.CaptureSession`、`RtPbrSurvey.Mp4Encoder`のCTest 3件が成功。
- `bin/CapturePort/step5-formats-after-empty-fix-run1`: PNG/EXR/GIF/MP4とForward/Path Tracing経路を含む12ケース成功。
- GUIでMP4録画中にClose Sceneを実行。64x64、固定30 FPSの2,168フレームがすべてH.264デコードに成功し、動画確定後にTopMenuへ遷移。
- GUIで終了確認の保存失敗後もアプリが終了せず、Cancel後に編集でき、Discardで終了できることを確認。
- 修正ビルドで終了確認ダイアログに保存エラー文と保存先が持続表示されることを確認。
- `bin/CapturePort/step5-save-failures-run2`: PNG/EXR/GIF/MP4の保存失敗で終了コード1、理由の`[ERROR]`ログ、D3D12 ERROR/CORRUPTIONなしを確認。
- `git diff --check`成功。

## 主な成果物

生成物はGitに追加しない。すべて`bin/CapturePort`以下。

- 通常形式検証: `bin/CapturePort/step5-formats-after-empty-fix-run1/report.json`
- 保存失敗検証: `bin/CapturePort/step5-save-failures-run2/report.json`
- MP4シーン終了: `bin/CapturePort/step5-mp4-scene-close-run4`
- Debugビルドログ: `bin/CapturePort/step5-empty-scene-build.log`, `bin/CapturePort/step5-modal-error-build.log`, `bin/CapturePort/step5-session-failure-build.log`
- 全結果と制限事項: `doc/branch/feature/tank-capture-gui-port-step5-report.md`

## 再現コマンド

PowerShellでリポジトリルートから実行する。指定出力先は新規である必要がある。

```powershell
python -B Tests/CaptureSession/validate_standalone_formats.py --output bin/CapturePort/step5-formats-retry
python -B Tests/CaptureSession/validate_standalone_save_failures.py --output bin/CapturePort/step5-save-failures-retry
```

実行にはDebugアプリ、`build/scene-document-tests/Debug/RtPbrSurvey.Mp4EncoderTests.exe`、および通常形式検証用のPillow/OpenEXR/numpyが必要。

## 次に行う確認

順番の目安:

1. SceneEditorで撮影中の編集拒否、保留Previewの反映、撮影中のGUIスライダー変更と保存画像への反映を確認する。
2. GUIでROI右クリックCancelとViewport境界付近のドラッグ/クランプを確認する。
3. GUIで無効設定と出力先エラーを発生させ、表示、状態遷移、再Start可否を確認する。CLIの保存失敗確認とは別に扱う。
4. MP4撮影中の実シーン切替を確認する。Close SceneによるTopMenu遷移の確認結果だけで、別シーンのロードは未確認。
5. 必要に応じ長時間/高解像度MP4と実プレイヤーの操作を確認する。現在はMedia Foundationデコーダーによる全フレーム検査まで。
6. 全変更に対しDebugビルド、関連CTest、D3D12 Debug Layer確認を行い、報告書と計画を更新する。

Step 4の旧13ケースは2026-10-05のMP4統合前結果。統合後の包括再実行ではない。
入力平面シーンによる描画経路テストは保存経路の回帰確認であり、画質やレンダラー間の視覚的一致を証明しない。

## Git / PR運用

この引継ぎ時点では、変更を含むPRのCIとマージ状態をGitHubで確認する。
PRがマージ済みなら、ローカル`main`を更新してPRのマージコミットを確認する。PRが未マージなら、未完了項目を明記したままPRをマージし、Step 5完了とは扱わない。
次の受け入れ確認はマージ済み`main`から新規ブランチを切って行う。

Commit / push / mergeは今回のユーザー依頼に含まれる。生成物、ログ、`bin`、`build`はコミット対象外。
