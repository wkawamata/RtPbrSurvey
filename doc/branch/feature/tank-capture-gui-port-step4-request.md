# Step 4 依頼書: 撮影競合と終了・シーン切替の保存制御

作成日: 2026-10-05
依頼内容: 実装、必要な修正、検証、結果報告。Commit は行わない。

## 1. 作業基点と禁止事項

依頼先 AI の WorkingDir は次を指定する。

```text
C:\work\RtPbrSurvey
```

編集、Git 状態の確認、ビルド、テストはこの WorkingDir を基点に行う。
依頼書の絶対パス:

```text
C:\work\RtPbrSurvey\doc\branch\feature\tank-capture-gui-port-step4-request.md
```

- 対象リポジトリ: RtPbrSurvey。
- 現在の作業ディレクトリ: C:/work/RtPbrSurvey。
- ブランチ: codex/tank-capture-gui-port。
- Step 3 までの基点コミット: 9db13e45985d9dd383f574e707123d874c364dd1。
- このブランチは origin に Push 済み。開始時に実際の HEAD、ブランチ、git status --short を確認する。
- 別の作業コピーを使用する場合は、実際の編集ディレクトリと基点を報告する。元の作業コピーの変更を上書きしない。
- AGENTS.md と tank-capture-gui-port-plan.md を読む。AGENTS.md に過去の作業ディレクトリが書かれていても、編集対象は実際の確認済み作業コピーとする。
- Commit、Push、PR 作成、Merge、Reset、既存ブランチの切替は行わない。差分を未コミットの状態で残す。
- 計画書の「各 Step をコミットする」という一般方針より、この依頼の Commit 禁止を優先する。
- TankPhysics 側は参照だけとし、C:/work/TankPhysicsSandbox と調査用 clone のソースを変更しない。
- CRLF、UTF-8 BOM なし、Allman、4スペースを守る。include の並べ替えや無関係なリファクタリングをしない。
- 既存のユーザー変更を保持する。画像・ログ・ビルド生成物をステージングしない。

## 2. 目的と現在の状態

Step 1〜3 で、独立した Capture Session GUI、F8 の開始・停止、共通 Mouse ROI、固定ステップ時計とシーン更新の接続を実装済み。
Step 4 では、競合した撮影要求と、保存中の終了・シーン切替を安全に処理する。
Step 3 までのコードにこの目的を妨げる不具合があれば、再現・原因確認のうえで必要な修正を含める。
「計画済みの項目を追加するだけ」には限定しないが、確認していない問題を推測で大きく作り替えない。

既存の撮影形式は PNG／EXR 連番とアニメーション GIF。Mouse ROI は基点に取り込み済み。
MP4 は共通対応 PR のマージ後に取り込む予定であり、この依頼ではエンコーダーを新規実装しない。
今後の形式追加に備え、共通 CaptureSession API を使い、ホスト側に形式固有の確定処理を重複実装しない。

## 3. 事前に確認する箇所

主な実装・調査対象:

- App/RtPbrSurveyApp.cpp、App/RtPbrSurveyApp.h。
- App/DebugUi.cpp。
- Platform/IApplication.h、Platform/Win32Application.cpp。
- Runtime/SceneRenderer.cpp、Runtime/CaptureSession.*、Runtime/CaptureSessionUi.*。
- Tests/CaptureSessionTests.cpp。
- Tests/CaptureSession/validate_standalone_timing.py。
- doc/branch/feature/tank-capture-gui-port-plan.md。

必要なら読み取り参照:

- C:/work/TankPhysicsSandbox-capture-review/src/TankSandboxApp.cpp の終了待ち・競合判定。
- 同 clone の Docs/capture-session.md と tests/CaptureSessionSmoke.ps1。

開始時点の具体的な調査ポイント:

1. RtPbrSurveyApp は OnCloseRequested を override していない。標準 WM_CLOSE は IApplication の既定実装により即座に DestroyWindow へ進む。
2. CloseRunningScene は直ちにシーンリソースを閉じる。ReturnToTopMenu、OpenFileScene、シーン読込、評価状態の復元など、同じリソースを差し替える他の経路も調べる。
3. App 内に DestroyWindow の直接呼出しが複数ある。共通の終了判定を迂回していないか確認する。OnDestroy で初めて待つ構成に頼らない。
4. SceneEditor の未保存変更確認を保持する。撮影終了待ちと保存／破棄／キャンセルの順序を定義し、既存の確認処理を迂回しない。
5. HasAutomatedCapture は設定パスや CLI セッションのフラグを参照している。設定があること、実行中であること、完了済みであることを混同していないか確認する。
6. m_captureSessionActive は CLI セッションの管理用。GUI セッションの実行状態をこのフラグだけで判定しない。Warmup／Recording／Draining は共通 status で判定する。
7. 単発 Screenshot ボタンはセッション中に無効化しているが、GUI 以外の要求経路、保存待ち、完了結果の回収も調べる。
8. Step 3 は更新後に撮影を予約し、更新を止めたフレームでも状態をポーリングする。P 一時停止中や readback 待ちでも Stop／終了待ちが進むことを保持する。

これらは修正候補の調査ポイントであり、すべてを検証済みの不具合として扱わない。

## 4. 必須の実装と修正方針

### 4.1 撮影要求の排他

- GUI ボタンと F8 の開始可否判定を共通化する。
- Capture Session、単発 Screenshot、CLI 撮影、既存の連続キャプチャ、HDR 診断撮影の競合を防ぐ。
- セッション実行中に別の開始要求で設定・時計・進捗を初期化し直さない。
- 待機中の終了／シーン切替があれば、新規撮影・シーン変更を開始させない。
- 拒否理由を GUI の状態表示に出す。
- 完了・失敗後に再開できる条件を明確にし、設定フラグの残留による永久的な無効化を避ける。
- 共通 renderer が既に行っている request ID による結果の振り分けと busy 判定を活用する。

### 4.2 Stop、ウィンドウ終了、シーン切替

- Stop は新しい要求を止め、受理済みで保存中の出力を処理し終えてから完了とする。
- 終了／シーン切替の要求は明示的な保留状態で保持し、撮影終了後に一度だけ実行する。
- 保留中は旧シーンと必要な GPU リソースを保持する。結果回収と必要な描画を続ける。
- 閉じるボタン、Escape、メニューからの戻り、シーン再読込・切替などを調べ、少なくとも同じ撮影リソースを破棄する経路を保護する。
- 保留中の重複要求の扱いを定義する。終了とシーン切替が競合した場合の優先順位を説明する。
- 新規終了要求で GUI 操作が無効になっても、保存結果のポーリングを止めない。
- GPU の同期 Wait を追加して GUI を固める方式や、保存待ちを busy loop で回す方式は避ける。
- 強制プロセス終了まで保証する必要はない。通常のユーザー終了と既存 CLI 終了を対象とする。

### 4.3 失敗と形式の確定

- 保存・出力確定の失敗を無視せず、状態とエラーを保持する。
- GIF の最終確定が終わる前にリソースを破棄しない。確定処理は共通層の責務を維持する。
- 失敗状態でも保留操作が永久に止まらないようにする。再試行するか、エラーを残して終了／切替を進めるかを明示する。
- CLI の正常完了は終了コード0、撮影失敗は非0とする。既存の DestroyWindow／WM_DESTROY 経路で常に0にならないか確認する。
- GUI 開始と CLI 開始の完了処理が互いに誤ってアプリ終了を起こさないようにする。

### 4.4 Step 3 までの修正

- 目的に関連する修正は実施してよい。原因、修正内容、検証を報告する。
- 固定時計、P／F、入力抑制、撮影要求の順序を壊さない。
- 動いているシーン、Camera、SceneEditor の GUI 編集が保存中の出力へ影響する場合は調べ、必要な範囲で制御する。
- 別問題で大きな改修が必要な場合は、未解決事項として根拠と推奨対応を残す。

## 5. 検証と完了条件

実装に合わせて意味のある回帰テストを追加・更新し、少なくとも次を確認する。

- Debug x64 ビルド成功。
- 共通 CaptureSession CTest 成功。
- 既存 validate_standalone_timing.py による固定 60／30 FPS、実時間、P／F／F8 の回帰。
- セッション中の単発撮影、単発の保存待ち中のセッション開始、GUI／CLI／自動撮影の競合要求を拒否。
- Warmup／Recording／Draining の各状態で Stop と通常ウィンドウ終了が完了する。
- 受理済み出力がある状態で終了・シーン切替を要求し、保存結果の回収後に一度だけ保留操作が実行される。
- P 一時停止中・固定ステップ readback 待ちでも終了待ちが進む。
- PNG の保存完了、EXR の保存完了、GIF の最終確定後のファイルが正常に読み取れる。
- SceneEditor の未保存確認でキャンセルした場合の状態を確認する。
- 保存不能な出力先などで失敗を発生させ、表示・保留操作・CLI 非0終了を確認する。
- 撮影終了後の再開始、通常の単発 Screenshot、既存 CLI 自動撮影に回帰がない。
- Debug 実行の D3D12 エラーを調べる。実行した全ケースのコマンドと結果を記録する。

生成物は bin/CapturePort/step4-* などの専用ディレクトリに保存し、既存生成物を上書きしない。
実 GPU／GUI を利用できない項目は未検証と明記し、実装済みという理由だけで完了扱いにしない。
Step 5 の全形式・全描画方式の網羅的検証をこの依頼で代替する必要はないが、今回変更した終了・競合の動作は確認する。

## 6. 成果物と報告

1. 未コミットのコード・テスト差分。
2. doc/branch/feature/tank-capture-gui-port-plan.md の Step 4 状態と進捗更新。
3. doc/branch/feature/tank-capture-gui-port-step4-report.md の実装・検証報告。

報告書には次を含める。

- 実際の編集ワークスペース、ブランチ、基点コミット。
- 調査で確認した問題と、候補だったが修正不要と判断した事項。
- 最終的な開始可否、終了／切替の保留状態と優先順位、失敗時の扱い。
- 変更ファイル、検証コマンドと結果、生成物の場所、未検証・未解決事項。
- git status --short の最終状態。Commit／Push をしていないこと。

最終回答の末尾に、機械判読可能な状態を1行で記載する。

Status: done

必要な作業が残っていて完了できない場合は、理由と残作業を書き、上の行を Status: blocked に置き換える。
