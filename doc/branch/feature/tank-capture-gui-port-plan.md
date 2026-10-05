# TankPhysics 撮影 GUI の RtPbrSurvey 移植計画

作成日: 2026-10-05
状態: Step 1 完了。Step 2 実装・自動確認済み、GUI の目視確認は未実施。Step 3 実装・自動確認済み。Step 4 レビュー後の修正実装・自動再検証済み（未コミット）。SceneEditor の実操作と GUI の受け入れ確認は未検証。Step 5 は継続作業。

## 目的と調査結果

TankPhysicsSandbox の Capture Session と同じ操作を、RtPbrSurvey 単体アプリから利用できるようにする。
共通の撮影処理と設定 GUI は既存実装を再利用し、ホストアプリの操作・時間同期・終了制御を整備する。

読み取り調査は C:/work/TankPhysicsSandbox-capture-review で実施した。
調査時の Tank コミットは 3bec4f9、参照する RtPbrSurvey サブモジュールは 6c327f30。
調査時の RtPbrSurvey 最新 main は 1eedb33。実装開始時には最新状態を再確認する。
元の C:/work/TankPhysicsSandbox は変更しない。

既存の共通機能には PNG／EXR 連番、アニメーション GIF、保存先・サブフォルダー、数値 ROI、FPS、ウォームアップ、フレーム数・時間の制限、GIF の繰り返し・フレーム破棄設定がある。
Mouse ROI と MP4 は共通機能の PR マージを取り込む前提で、最終的な移植・検証範囲に含める。共通エンコーダーや選択処理は重複実装しない。
2026-10-05 の開始時点で Mouse ROI は PR #90 としてマージ済み。MP4 は共通 GUI に未対応と表示されており、対応 PR のマージ後に取り込んで検証する。MP4 待ちで既存形式の移植を止めない。
PNG／GIF は UI を含む最終出力、EXR は UI を含まないトーンマップ前の線形 HDR シーンカラーを保存する。

## Step 1: 最新 main と実装基点の確認

- [x] 作業ツリーと既存ブランチの状態を確認し、ユーザーの変更を保護する。
- [x] 最新 main を基点に codex/ 接頭辞の移植ブランチを作成する。
- [x] 共通撮影 API、GIF 対応、CLI 撮影、既存の自動撮影処理を再確認する。
- [x] Debug x64 ビルドを実行する。

完了条件: 基点コミットと既存機能の確認結果を記録し、変更前の Debug ビルドが通る。

## Step 2: 撮影 GUI と操作の移植

- [x] Capture Session を独立した GUI 項目として表示する。
- [x] 保存先・形式・FPS・時間・GIF 設定には共通 CaptureSessionUi を利用する。
- [x] F8 で撮影開始・停止を切り替えられるようにする。
- [x] 数値 ROI の枠をプレビューし、撮影中は枠を非表示にする。
- [x] 状態、保存数、ドロップ数、出力先、エラーを確認できるようにする。

主な変更先: App/DebugUi.cpp、App/RtPbrSurveyApp.cpp、App/RtPbrSurveyApp.h。
共通 GUI の変更が必要な場合だけ Runtime/CaptureSessionUi.* を変更する。

完了条件: GUI から設定・開始・停止でき、F8 と ROI 表示を確認できる。Debug ビルドが通る。
この段階の操作確認だけでは、固定ステップや終了時の保存完了は検証済みとしない。

## Step 3: 撮影とシーン更新の時間同期

- [x] 実時間撮影と固定ステップ撮影の時計を分ける。
- [x] 固定ステップにも実時間を渡している既存の接続を修正する。
- [x] 撮影用のシーン更新時刻・更新幅を定義し、シーンとカメラを同期させる。
- [x] 固定ステップ撮影の保存待ちではシーン・カメラ更新を止め、描画と保存結果の確認を続ける。
- [x] 再生、一時停止、コマ送りと撮影時計の関係を明示して実装する。

Tank の物理時計をそのままコピーせず、RtPbrSurvey のシーン更新とフレーム制御へ接続する。

完了条件: 固定ステップの撮影時刻とシーン状態が一致し、GPU readback の待ち時間によって撮影間隔が変わらない。停止操作と保存結果の確認を継続できる。

## Step 4: 撮影の競合と終了処理

- [x] 単発 Screenshot、CLI 撮影、既存の連続キャプチャとの競合を防ぐ。
- [x] GUI とショートカットで同じ開始可否判定を使用する。
- [x] Stop は処理中の保存を完了してから撮影終了とする。
- [x] ウィンドウ終了とシーン切替では、保存中のフレームを完了させてから処理を進める。
- [x] 保存失敗時の状態、表示、CLI 終了コードを整理する。
- [x] レビュー後の修正と終了・競合13ケース、時間同期4ケース、故障注入を自動再検証する。
- [ ] SceneEditor の未保存確認 Save／Discard／Cancel、保存失敗、再編集・再終了を実操作で確認する。
- [ ] 撮影中の編集拒否と保留 Preview の反映を GUI で確認する。

完了条件: 競合した開始要求が拒否され、途中停止・終了・シーン切替で保存中のフレームが失われない。既存の撮影自動化も動作する。

## Step 5: GUI・GPU 検証と報告

- [ ] PNG／EXR 連番とアニメーション GIF を実 GPU で検証する。
- [ ] 共通 MP4 対応 PR のマージ後に取り込み、MP4 の生成・再生・終了時の確定を検証する。
- [ ] Mouse ROI の順方向・逆方向ドラッグ、キャンセル、出力座標への変換、選択中のカメラ操作抑制を確認する。
- [ ] 全画面、ROI、保存先・サブフォルダー、GIF の繰り返し設定を確認する。
- [ ] F8、途中停止、撮影中のウィンドウ終了、シーン切替を確認する。
- [ ] 固定ステップの撮影間隔、再生・一時停止・コマ送りの挙動を確認する。
- [ ] 不正設定や保存失敗を確認する。
- [ ] Debug ビルドで D3D12 Debug Layer エラーを調べる。
- [ ] 結果、再現コマンド、制限事項、未検証項目を報告書に残す。

参考: 調査用 Tank の tests/CaptureSessionSmoke.ps1 と Docs/capture-session.md。
Tank の物理シーンに依存する検証は RtPbrSurvey のシーンに合わせて置き換える。
生成画像・ログ・ビルド出力は通常コミットしない。

完了条件: 上記の GUI 操作と撮影出力が確認でき、必要な回帰テストと D3D12 確認が通る。報告書から検証範囲を判断できる。

## 進め方

各 Step をビルド可能でレビューしやすい小さなコミットにする。
Step 1〜2 で操作を確認できる状態を作り、その後に時間同期と終了処理を完成させる。
実装中に判明した制約や方針変更はこの計画書に反映する。
PR 作成・Push・マージはユーザーの指示に従う。

## 実装開始記録

- 2026-10-05: origin/main の 81a2c45 を基点に codex/tank-capture-gui-port を作成。既存のローカル main は変更しない。
- Mouse ROI は共通 CaptureSessionUi::DrawRegionOverlay とアプリ入力の抑制処理が既に接続済み。既存実装を保持する。
- MP4 対応は未マージ。共通の形式選択と API を維持して後続変更を取り込む。
- 基点の Debug x64 ビルド成功。既存のマクロ再定義・vcpkg 警告は残るが、ビルドエラーはない。

## Step 2 実装・検証記録

実装コミット: d6142f2。

- Capture Session を Screenshot 配下から独立した折り畳み項目に変更。
- GUI と F8 は同じアクション処理を利用。Running と SceneEditorEdit で F8 を使用できる。
- 既存の撮影自動化が設定されている場合、新しい GUI/F8 撮影の開始を拒否。セッション中の単発 Capture PNG ボタンを無効化。
- 共通 Mouse ROI の入力抑制・描画接続を保持。撮影開始では選択状態をキャンセル。
- 共通 GUI の PNG／EXR／GIF 設定と MP4 未対応表示を維持。

確認結果:

- 移植前・移植後の Debug x64 ビルド成功。
- CTest RtPbrSurvey.CaptureSession 成功。Mouse ROI の座標変換とドラッグ・キャンセル、GUI の Stop ボタン、GIF メタデータ・出力パス、保存待ちなどの既存回帰を含む。
- 専用のアプリプロセスへ F8 の Windows キーメッセージを送る実行確認成功。開始後に PNG の保存を待ち、再度 F8 を送って出力が止まることを確認。1920×1080 の PNG 2枚、終了コード0、D3D12 エラー0。
- 実行結果とログは bin/CapturePort/step2-f8、ビルドログは bin/capture-port-baseline-build.log と bin/capture-port-step2-build.log に保持。生成物はコミットしない。

未検証・残作業:

- GUI の配置、手動ボタン操作、実マウスでの ROI 操作の目視確認は未実施。Step 2 のチェックは実装完了を示し、完了条件の目視確認はまだ残る。
- 固定ステップの時計と更新制御、終了・シーン切替時の保存完了は Step 3〜4 で整備する。
- MP4 は後続の共通対応 PR マージ後に取り込み、Step 5 で生成・再生を検証する。

## Step 3 実装・検証記録

- シーン更新コールバックの最後で、シーン公開後に撮影要求を出す。保存待ち・P 一時停止で更新コールバックを呼ばないフレームも、撮影状態のポーリングと描画を続ける。
- CLI と GUI の開始時に撮影時計を初期化。固定ステップは指定 FPS の 1/FPS 秒でシーンとキーボードカメラを更新し、その更新時だけ撮影用シミュレーション時計を進める。
- 固定ステップの readback 待ちではシーン・カメラ更新を抑制。マウスによるカメラ移動も待機中・P 一時停止中は抑制し、マウスアップでドラッグ状態は解放できる。
- P は固定ステップ時計とフレーム更新を一時停止。F は保存待ちが解消したら一度だけ更新し、要求を消費する。Space はシーンアニメーションだけを止め、撮影時計は止めない。
- 毎 idle で実時間差分を更新するため、一時停止や保存待ちの時間を通常更新の deltaTime にまとめて渡さない。
- ウォームアップは既存の描画フレーム数の定義を維持。P 一時停止中でも描画フレームのウォームアップは進み、初回の静止画像は取得可能。以後の固定時計は F／再開まで進まない。

検証:

- Debug x64 ビルド成功、共通 CaptureSession CTest 成功。
- Tests/CaptureSession/validate_standalone_timing.py を追加。CLI の固定 60 FPS／30 FPS、実時間 30 FPS、固定時計の P／F／F8 を専用プロセスで自動確認。
- 固定 60 FPS の撮影時刻: 0.066666667、0.083333333、0.100000000 秒。対応する描画フレーム: 3、6、9。
- 固定 30 FPS の撮影時刻: 0.133333333、0.166666667、0.200000000 秒。対応する描画フレーム: 3、6、9。
- P／F ケースは描画フレーム3から186までの間に、F を送った一度だけ 1/60 秒進行。F8 で停止・保存完了後に正常終了。
- 4ケース合計11枚の 64×64 ROI PNG を検証。全プロセス終了コード0、D3D12 エラー0。
- 実時間ケースはシミュレーション時計を使用せず、通常の実時間更新と撮影が続くことを確認。

再実行例（出力先は既存ケースディレクトリと重複しない新しい場所を指定）:

```powershell
python -B Tests/CaptureSession/validate_standalone_timing.py --output bin/CapturePort/step3-timing-retry
```

測定結果・ログ・PNG は bin/CapturePort/step3-timing、ビルドログは bin/capture-port-step3-build.log に保持。ソース・実行ファイルのハッシュを測定結果に記録。今回の自動測定は既知平面シーンで実施し、動くシーンの目視確認、GUI スライダーによる編集との関係、全描画方式での回帰は Step 5 に残す。終了・シーン切替時の保存制御は Step 4 で整備する。

## Step 4 レビュー後の修正・再検証記録

実装と修正差分は未コミット。WorkingDir は C:\work\RtPbrSurvey、HEAD は 9db13e4 のまま。
詳細: [Step 4 報告書](C:/work/RtPbrSurvey/doc/branch/feature/tank-capture-gui-port-step4-report.md)。

- 開始判定を CaptureRequestGate に集約し、各判断の直前に状態を更新する。GUI、F8、単発 Capture PNG は同じ開始可否を使う。
- 終了・シーン操作は撮影を Stop してから受理済み出力と形式確定を待つ。保留 action を消費して一度だけ実行し、最初の対象パス・選択シーンを保持する。
- SceneEditor の未保存確認とウィンドウ終了を統合。保留要求がある場合にだけ、撮影出力が解消した後で確認を開く。Save 失敗は終了を保留し、Cancel は要求を解放する。
- Preview 再構築要求を保持し、最新 Document から実行する。Undo／Redo、追加・削除、Transform／Material 等の UI は Document 変更前に編集を拒否する。
- CLI 正常終了0、撮影失敗1。Win32の終了コードを int で返す。
- 自動撮影の予約・in-flight と完了・失敗を区別する。設定パスだけで開始を拒否する HasAutomatedCapture は削除し、自動撮影中の結果はアプリ側、完了後の GUI 単発結果は GUI 側が回収する。
- 終了検証は期待終了コード、必須checks、保存数、画像デコード、GPUエラーを assert する。Windows入力対象は PID と RtPbrSurveyAppClass の双方で選ぶ。故障注入の失敗理由も確認する。

確認済み:

- Debug x64ビルド成功、CaptureSession／ScreenshotのCTest 2件通過。
- 終了・競合13ケース通過。P停止中のWM_CLOSE単独、Warmup終了・Stop、Draining終了、シーン閉鎖、予約競合、正常完了・失敗後のF8再開始を確認。
- GIFをPillow、EXRをOpenEXRで実デコード。受理済み出力の保存を確認。D3D12 ERROR／CORRUPTION 0。
- 時間同期4ケース通過。固定60／30 FPS、実時間、P／F／F8。合計11枚、全終了コード0。
- 誤った期待終了コードの故障注入で AssertionError とスクリプト終了コード1を確認。
- 両スクリプトの既存出力先拒否と report.json のハッシュ不変を確認。
- 生成物は bin/CapturePort/step4-revision-close-final、step4-revision-timing-final、step4-revision-injected-failure-run2、step4-revision-build-run1。途中結果と出力先重複による旧JSONの上書き事故は報告書に記録した。

未検証:

- SceneEditor の Save／Discard／Cancel、保存失敗、編集拒否・保留Preview反映の実操作。
- GUIボタンと実マウスROIの目視確認、動くシーン・全描画方式・EXR連番・GUI設定変更との関係。
- MP4は共通対応PRマージ待ち。

Step 4 の実装・自動再検証は完了した。上記のGUI受け入れ項目と移植全体の最終確認は引き続き残る。Commit／Pushは行っていない。
