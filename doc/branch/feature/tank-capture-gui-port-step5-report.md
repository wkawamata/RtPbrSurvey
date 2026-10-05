# Capture GUI Step 5: MP4 を含む検証記録

日付: 2026-10-06
WorkingDir: C:\work\RtPbrSurvey
ブランチ: codex/capture-gui-step5
基点: 9c302a74d88e444656428e7d884aef76612246ba (PR #92 マージ後)
状態: 検証と空シーン修正は未コミット。残る受け入れ項目は末尾に記載する。

## MP4 統合

共通 MP4 対応は PR #91、Step 4 との統合は PR #92 で main に反映済み。
今回、重複したエンコーダーを作らず、共通 CaptureSessionUi と Windows Media Foundation H.264 エンコーダーを実アプリで検証した。

## CLI / 実 GPU

Tests/CaptureSession/validate_standalone_formats.py を追加した。
PNG は Pillow で全画素を読み込み、EXR は OpenEXR で全チャネルの寸法と有限値、GIF は全フレームと loop=2 を確認する。
MP4 は RtPbrSurvey.Mp4EncoderTests.exe の Media Foundation デコーダーで全フレームを読み、H.264、寸法、FPS、フレーム数、時刻と終端時間を確認する。
受理数とデコード数の一致、固定時計の 1/FPS 間隔、終了コード0、D3D12 ERROR/CORRUPTION と保存エラーの不在を assert する。

| ケース | 設定 / 検証 |
| --- | --- |
| png-roi / png-full | 64x64・3枚・60 FPS / 1920x1080・2枚・30 FPS |
| exr-sequence | 64x64・3枚・30 FPS、線形HDRの実デコード |
| gif-repeat | 64x64・3フレーム・30 FPS、loop=2 |
| mp4-odd-roi-60 | ROI 63x47、H.264 64x48へパディング、5フレーム・60 FPS |
| mp4-roi-30 | 64x64・5フレーム・30 FPS |
| mp4-full | 1920x1080・3フレーム・30 FPS |
| mp4-real-time | 64x64・60 FPS指定・1.5秒、受理フレーム数とデコード数、時間軸 |
| png/mp4-forward | Forward、64x64・3フレーム・30 FPS |
| png/mp4-path-tracing | Path Tracing、64x64・3フレーム・30 FPS |

上記以外の既定ケースは Deferred。全ケースで outputs/nested サブフォルダーと basename=movie を指定した。
MP4 のビットレート設定は 8 Mbps。指定値の受理を確認したもので、短い動画の実ファイル平均ビットレートが8 Mbpsであると保証する測定ではない。

検証成果物:

- bin/CapturePort/step5-formats-run1: 修正前の8形式ケース全通過。
- bin/CapturePort/step5-formats-realtime-run2: デコード数一致を追加後、実時間MP4の30フレームを確認。
- bin/CapturePort/step5-render-paths-run1: 修正前のForward / Path Tracing 4ケース全通過。
- bin/CapturePort/step5-formats-after-empty-fix-run1: 空シーン修正後の全12ケースが通過。実時間MP4は受理22/デコード22フレーム。詳細は report.json を参照。

各 report.json に CLI コマンド、受理フレーム時刻、実行ファイル・スクリプト・デコーダー・出力の SHA256 を保存する。新規出力ルートだけを許可し、既存ルートは拒否する。
入力シーンは Assets/Scenes/PathTracingValidation/input-plane/scene.json。ROI が均一な平面なので、全描画方式の確認は撮影・保存経路の回帰であり、画質の正しさや方式間の見た目の比較を証明しない。

再現例（既存でない出力先を指定）:

```powershell
python -B Tests/CaptureSession/validate_standalone_formats.py --output bin/CapturePort/step5-formats-retry
```

前提: Debugアプリと build/scene-document-tests/Debug/RtPbrSurvey.Mp4EncoderTests.exe のビルド。Python に Pillow / OpenEXR / numpy が必要。今回の環境では bin/CapturePort/python-packages と bin/PathTracingValidation/python-packages を利用した。

## GUI 実操作

自身が起動した Debug アプリを対象に、マウスとキーボードで操作した。
シーンは Animated Shadow Grid。GUI動画の保存先は bin/CapturePort/step5-gui-run1。

- Output Dir と MP4 形式をGUIで指定し、Startボタンから撮影。実時間60 FPS指定で60フレーム保存、128ドロップを表示。最初の capture.mp4 は作成確認のみで、全フレームデコードは未実施。
- Fixed Step をONにし、F8で開始。capture_000001.mp4 は192x142、60フレーム、60 FPSで全デコードと時間検証を通過。ドロップ0。
- Spaceでシーンアニメーションを開始。動くCubeを目視し、capture_000002.mp4 を同条件で保存・全デコード。動画中の移動量を数値比較したわけではない。
- Pで一時停止してF8開始。受理/保存数1で停止を維持し、Fで一度だけ2へ増加。Stopボタンで確定した capture_000003.mp4 は2フレームとして全デコード。
- P停止中の撮影をF8開始し、1フレーム保存後Alt+F4で終了。capture_000004.mp4 は1フレームとして全デコードし、プロセス終了を確認。
- 連続撮影では capture_000001 以降に採番され、最初の capture.mp4 のSHA256が変わらないことを確認。
- Select ROIから順方向と逆方向へ実マウスドラッグ。どちらも出力ROI x=1315, y=576, width=191, height=142 を表示し、H.264は192x142へパディングされた。
- ROI選択中にカメラの床・Cubeの構図が移動しないことを目視。Escapeで選択をキャンセルし、前の数値ROIとシーンが維持された。

GUI起動では LogToFile を指定していない。GUI実操作のD3D12エラー不在は主張せず、Debug Layer確認はCLIケースに限る。

## 発見した不具合と修正

SceneEditorのNew Creationで、空Meshに対し CreateSceneGeometryBuffers 内の !mesh.vertices.empty() assertion が発生した。
Engine/RtPbrSurveyEngine.cpp の ReloadSceneResources で、既存GPUリソースを解放後、Meshがない/頂点0ならGPUジオメトリ作成を省略して編集画面を維持する。
ダミーObjectをシーンデータへ追加しない。可視Mesh追加時は通常の再ロード経路でGPUリソースを作成する。

修正後、実操作で次を確認:

- New Creation: Nodes 0の編集画面を維持、assertionなし。
- Add Cube: Nodes 1となりCubeを描画。
- Undo: Nodes 0の空シーンへ戻り、assertionなし。
- Redo: Nodes 1となりCube描画が復帰。
- Back to TopMenuの未保存確認でCancel: DocumentとCubeを維持し編集へ戻る。
- Save Pathを bin/CapturePort/step5-editor-run1/scene.json とし、Save and Continue: scene.jsonとrender-preset.jsonを保存してTopMenuへ遷移。
- 保存後Alt+F4で終了。既存ユーザーのシーンを編集・上書きしていない。
- 保存したscene.jsonをGUIから再ロードし、Cubeの描画復帰を確認。
- Sphere追加後、通常ファイルblocker.txtの配下をSave As先に指定。Could not create scene directoryを表示し、Nodes 2とModifiedを維持。
- Save As失敗後Alt+F4、Cancelで編集へ復帰。Add PlaneでNodes 3へ変更でき、再度Alt+F4のDiscardで終了。保存済みscene.jsonはCube 1個のまま。

Debug x64 MSBuild成功。ログ: bin/CapturePort/step5-empty-scene-build.log。既存マクロ/C4819警告あり。
修正後 CaptureSession / Screenshot / Mp4Encoder の CTest 3件通過。

## 追加確認: MP4撮影中のClose Sceneと終了前保存失敗

bin/CapturePort/step5-mp4-scene-close-run4 にCLIでMP4撮影を開始した。
64x64、固定30 FPS、上限100000フレーム、ExitAfterCaptureなし。GUIのRecording表示を確認し、Close Sceneを押すとTopMenuへ戻った。
受理2168フレームがすべてH.264としてデコードでき、64x64・30 FPSの時刻検証も通過。約72.27秒の動画が確定した。
LogToFileのD3D12 ERROR/CORRUPTIONとCapture failedは0。直接別シーンをロードする操作とは区別する。
準備中のrun1/run2はBaseName指定不足で失敗、run3はGUIへ接続できず中断。通過結果に含めない。

同じアプリのSceneEditorで新規未保存Documentを作り、通常ファイルblocker.txtの配下をSave Pathに指定した。
Alt+F4のSave and Continueが失敗しても終了せず、Cancelで編集へ戻り、Add EmptyでNodes 0から1へ変更できた。
再度Alt+F4のDiscardでアプリを終了した。ユーザーの保存済みシーンは変更していない。

この操作で、保存失敗文がクリックした1フレームだけ描画される不具合を発見した。
App/SceneEditorUi.cppでSave failedをダイアログが開いている間毎フレーム表示するよう修正した。
Debugビルド成功。ログ: bin/CapturePort/step5-modal-error-build.log。再起動した修正版でSave and Continueの失敗理由と保存先がダイアログに残ることを実画面で確認し、Discardで終了した。

## 残る受け入れ確認

Step 5全体を完了扱いにはしない。

- 撮影中の編集拒否、保留Preview反映、GUIスライダー操作と保存中出力の関係。
- MP4撮影中のシーン切替。Step 4の既存13ケースは2026-10-05の結果で、MP4統合後の再実行結果ではない。
- GUIでの不正設定/保存失敗。単体CTestの拒否・失敗テストと区別する。
- 実マウスROIの右クリックCancel、画面境界外ドラッグ/クランプ。
- 全描画方式の見た目、長時間/高解像度MP4、実再生アプリの操作確認。今回のMP4再生検証はMedia Foundationによる全デコード。

生成物はbin配下に保持し、Commit対象にしない。
