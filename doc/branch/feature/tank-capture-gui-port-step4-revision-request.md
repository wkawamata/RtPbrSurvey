# Step 4 レビュー後の修正依頼

作成日: 2026-10-05
対象: Quen3.8 による Step 4 の未コミット実装。
判定: 現状は完了扱いにできない。以下を修正・再検証する。

## 作業環境と制約

```text
WorkingDir: C:\work\RtPbrSurvey
Branch: codex/tank-capture-gui-port
Base commit: 9db13e45985d9dd383f574e707123d874c364dd1
```

現在の未コミット差分を引き継ぐ。最初に git status --short と HEAD を確認し、既存の変更を破棄しない。
AGENTS.md、元の Step 4 依頼書、計画書、Step 4 報告書を読む。
この依頼では Commit、Push、PR 作成、Merge、Reset、ブランチ切替を行わない。
修正・テスト・報告の差分は未コミットで残す。TankPhysics 側を編集しない。
CRLF、UTF-8 BOM なし、Allman、4スペースを守り、無関係なリファクタリングを避ける。

元の依頼書:
C:/work/RtPbrSurvey/doc/branch/feature/tank-capture-gui-port-step4-request.md

## 1. [P1] 終了／シーン閉鎖要求で撮影を停止する

対象: App/RtPbrSurveyApp.cpp の RequestCloseApplication、RequestCloseRunningScene と関連する保留処理。

現状は保留操作を記録するだけで StopCaptureSession を呼ばず、Recording が継続する。
固定ステップを P で一時停止すると撮影の終了条件に到達せず、閉じる要求後もアプリが終了しない。
レビューの実行確認では、P 停止中に WM_CLOSE を送っても受理数1のままアプリが残り、追加の F8 Stop で正常終了した。

修正方針:

- 終了／シーン閉鎖要求を受けたら、新しい撮影を止める。
- 受理済み出力の保存と共通層の形式確定だけを待ち、終わったら保留操作を一度だけ実行する。
- P 一時停止中も結果回収と必要な描画を継続する。
- 終了待ち中の重複要求、新規開始の拒否、失敗時の解放を整理する。

再検証:

- P 停止中に F8 を先に送らず、WM_CLOSE だけで保存後に終了する。
- Warmup、Recording、Draining の各状態で閉じる要求が完了する。
- シーン閉鎖・切替でも撮影全体のフレーム上限まで継続せず、受理済み出力を処理した後に進む。
- 撮影開始後の終了要求で、新しいフレームが無制限に受理され続けない。

## 2. [P1] SceneEditor の未保存確認をアプリ終了へ接続する

対象: OnCloseRequested → RequestCloseApplication → DestroyWindow の経路と SceneEditor の pending decision。

現状は SceneEditor の未保存状態を確認せずアプリを閉じる。撮影終了待ちと未保存確認の統合が未完成。

修正方針:

- 撮影出力の処理、保存／破棄／キャンセル判断、アプリ終了の順序を明示して接続する。
- Save 失敗では終了せず、エラーを保持する。
- Cancel でアプリ終了の保留状態を解除し、編集を継続できるようにする。
- 終了とシーン切替が重複しても、確認を迂回したり同じ操作を二度実行したりしない。

再検証:

- 未保存 Document の通常終了で Save／Discard／Cancel が正しく機能する。
- 撮影中の終了、保存失敗、Cancel 後の再編集・再終了を確認する。
- Cancel の実操作ができない環境では未検証と明記する。

## 3. [P2] Preview 再構築要求を捨てない

対象: RebuildSceneEditorPreview と App/SceneEditorUi.cpp の編集呼出し元。

現状は撮影中に false を返すだけで、再構築を保留していない。
呼出し元は先に Document を編集しているため、撮影終了後も Document と表示が食い違う。

修正方針:

- 再構築要求を保持し、撮影出力が処理された後に最新 Document から再構築する、または Document 変更前に編集操作を拒否する。
- 「deferred」と表示するだけで要求を破棄しない。
- Undo／Redo、追加・削除、Transform／Material 編集など、同じ再構築経路を使う操作を確認する。
- 同じ問題があるシーン読込・切替経路も確認し、保留した対象を実際に実行できるようにする。

再検証:

- 撮影中に編集操作を試し、終了後に表示と Document が一致する。
- 許可しない操作は Document も変更されない。
- 複数回の編集／再構築要求でリソース破棄や二重実行が起きない。

## 4. [P2] 自動検証の失敗を終了ステータスへ反映する

対象: Tests/CaptureSession/validate_standalone_close.py。

現状は checks の真偽やアプリ終了コードを記録するだけで、期待値を検証していない。
出力欠落や誤った終了コードでもスクリプトが正常終了し、「全ケース通過」と報告できてしまう。

修正方針:

- 正常ケースの終了コード0、失敗ケースの期待する非0コードを assert する。
- 各 checks が成功したかを assert し、false または必須チェックの欠落で失敗する。
- 正常ケースの D3D12 ERROR／CORRUPTION を検出する。意図した保存失敗ログは GPU エラーと区別する。
- GIF はヘッダーと末尾バイトだけでなく、デコード可能で期待するフレームを持つことを確認する。
- テスト故障を意図的に注入し、スクリプトが非0終了することも確認する。

Windows 操作対象は PID だけでなく RtPbrSurveyAppClass で絞る。
レビューの再現確認では、PID のみの列挙でキーメッセージが意図したアプリウィンドウに届かないケースがあった。

## 5. 報告との差: 自動撮影の設定と完了を整理する

対象: HasAutomatedCapture と UpdateCaptureRequestGate。

報告では設定・実行中・完了を整理済みとされているが、HasAutomatedCapture は以前と同じ設定パス判定のまま。
-ExitAfterCapture なしで単発自動撮影が完了しても、capturePath が残り、新しい撮影が拒否され続ける。

修正方針:

- 将来の予約、実行中・保存待ち、正常完了、失敗を区別する。
- 自動撮影終了後に新しい GUI セッションや単発 Screenshot を利用できるようにする。
- GUI 状態表示の結果回収で、自動撮影の終了処理が取り残されないことを確認する。

再検証:

- -ExitAfterCapture なしの単発自動撮影完了後、F8 と GUI ボタンで開始できる。
- 連続撮影計画の未実行予約が残る間は競合要求を拒否する。
- 失敗後の再開始条件も確認する。

## 完了前の確認と成果物

レビューでは既存の CaptureSession と Screenshot の CTest 2件は成功したが、上記問題は残っている。
修正後に Debug ビルド、両 CTest、validate_standalone_timing.py、修正した close 検証を実行する。
必要な SceneEditor・Warmup・EXR の確認も行い、できない項目は明記する。
生成物は bin/CapturePort/step4-revision-* の新しいディレクトリへ保存し、以前の結果を上書きしない。

- コード・テスト差分は未コミットで残す。
- Step 4 報告書と計画書を更新し、実際に完了した項目と未検証項目を区別する。
- 本依頼の各項目について、修正内容、再現・検証コマンド、結果、生成物の場所を報告する。
- 最終 git status --short を提示し、Commit／Push していないことを明記する。
- 必須の修正・検証が残る場合は Status: done としない。

最終回答の末尾:

Status: done

完了できない場合は理由と残作業を書き、Status: blocked とする。
