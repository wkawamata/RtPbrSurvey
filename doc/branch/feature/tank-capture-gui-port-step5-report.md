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

## 追加確認: 4形式のCLI保存失敗

前回の修正と検証記録を c74c2e8 にCommitした。Pushはしていない。
Tests/CaptureSession/validate_standalone_save_failures.py を追加し、通常ファイルblocker.txtの配下を撮影出力先に指定する。
既存でない成果物ルートだけを使用し、PNG/EXR/GIF/MP4を順番に起動する。GUI入力は行わない。

最初のrun1ではPNGの終了コード1を確認したが、連続撮影失敗の理由がLogToFileに出力されず、検証は失敗した。
App/RtPbrSurveyApp.cppのOnIdleでCaptureSessionState::Failedを処理するとき、[ERROR] Capture session failedと理由をログに記録してflushするよう修正した。

修正後の bin/CapturePort/step5-save-failures-run2 は4形式すべて通過。
終了コード1、理由のログ出力、D3D12 ERROR/CORRUPTIONなし、blocker.txtを上書きしないことを確認した。
PNG/EXR/GIFはcreate_directories失敗、MP4はUnable to create MP4 output folderを記録する。
report.jsonに再現コマンド、実行ファイルSHA256、基点Commit、終了コードとエラーを保持する。
これはCLIの実行時保存失敗確認であり、GUIの不正設定入力や失敗後再Startの確認を代替しない。
Debugビルド成功（bin/CapturePort/step5-session-failure-build.log、既存警告あり）。CaptureSession/Screenshot/Mp4EncoderのCTest 3件通過。
この追加修正・スクリプト・報告書更新は未コミット。

## 2026-10-08 継続: GUI 制御の回帰と保存失敗の再確認

`codex/capture-gui-step5-completion` を最新 main (`49aca62`) から作成した。

`Tests/CaptureSessionTests.cpp` に次の focused regression を追加した。

- Capture Session が `Failed` の terminal state から再 Start でき、次の Start が `Recording` に戻る。
- Start は terminal status では有効、`Recording` 中と host capture gate が理由付きで block している間は無効である。
- 既存の ImGui synthetic click を利用し、可変高 status 表示があっても Start/Stop の操作契約を独立して確認する。

CMake の `RtPbrSurvey.CaptureSession` は通過した。Debug x64 MSBuild は成功した。
最初のビルドは中断済みの generated `obj/x64/Debug/vc143.pdb` と競合したため、生成 PDB のみを再作成した。
node editor / ImGuizmo の既存 object について LNK4099 の debug-info warning は残るが、リンクと実行ファイル生成は成功した。

`Tests/CaptureSession/validate_standalone_save_failures.py` を新規 output root
`bin/CapturePort/step5-completion-save-failures-20261008` で再実行した。PNG/EXR/GIF/MP4
すべてで期待どおり終了コード 1、`[ERROR] Capture session failed`、D3D12 ERROR/CORRUPTION なしを確認した。

native window の列挙失敗は、native 操作無効の別 API を選んだ操作側の誤りだった。
Windows 用 `@oai/sky` API で RtPbrSurvey window を取得し、実マウス GUI 検証を再開した。
SceneEditor で新規 Document に Cube を追加し、検証専用の生成物フォルダへ保存した。
SceneEditor の UI は共通 Capture Session パネルを表示していなかったため、編集禁止範囲の外へ
パネルを追加した。これにより撮影中にも Stop と status/error を参照できる。

### 今回の実 GUI 確認

- SceneEditor の Capture Session パネル表示。
- ROI 選択を右クリックで Cancel。ROI 未設定のまま通常操作へ復帰。
- FPS 0 で Start を拒否し、`framesPerSecond must be greater than zero` を表示。
- 書き込み不可の working directory に相対出力し、MP4 が `Failed` と
  `Unable to create MP4 output folder: Access is denied.` を表示。アプリは継続。
- 絶対出力先を生成物フォルダに変更し、Failed から MP4 再 Start に成功。
- Recording 中は撮影設定と SceneEditor の Add Empty / Add Cube などが無効。
  Add Empty への実クリックでも scene に変化はなかった。
- GUI Stop 後に Completed、accepted/saved 16/16、dropped 0。編集ボタンが復帰。
- ROI を右下端までドラッグし、X 1399、Y 669、Width 519、Height 410。
  1920x1080 の出力範囲を超えない。画面外への drag は未確認。

生成 MP4: `bin/CapturePort/step5-gui-20261008/capture.mp4`。
Media Foundation inspector は H.264 1920x1080、全16フレームをデコードした。
今回は fixed-step OFF の実時間モードであるため、固定刻み時刻を前提とする
inspector の FPS/時刻/duration assertion は不一致となった。固定刻み検証成功とは扱わない。

追加変更後の Debug x64 MSBuild 成功。vcpkg 自動 import がない shell では
`ForceImportAfterCppProps` / `ForceImportBeforeCppTargets` を既存 vcpkg props/targets に
指定してビルドした。CMake Mp4EncoderTests を生成後、Screenshot / CaptureSession /
Mp4Encoder の CTest 3/3 通過。

## 2026-10-08 継続: MP4 中の新規シーン切替

PR #94 (`66ab0a6`) を含む main から `codex/capture-scene-transition-validation` を作成した。
Windows 操作手順書の `@oai/sky` を利用して実 GUI 検証を継続した。

- Cube Document を生成物フォルダへ保存し、PT input-plane Document の Load 成功を確認。
  この回は録画上限 60 frames で Completed になったため、録画中 Load の確実な証拠とはしない。
- Frame limit / Duration limit を両方 OFF にすると、Start は
  `Capture session requires a frame limit or duration.` の理由付きで拒否された。
- 上限 3600 frames、FPS 1、実時間、MP4、ROI 519x410 に設定して再 Start。
  active 表示と無効になった撮影設定を確認した直後に New Creation を要求。
- 新しい空 Document (`Nodes: 0`) に切り替わり、MP4 は Completed、accepted/saved 7/7、
  dropped 0。3600 frames より前の終了であり、シーン操作による停止・保存完了を確認。
- 生成物: `bin/CapturePort/step5-gui-20261008/scene-load-transition-unlimited.mp4`。
  名前の unlimited は検証時の識別子で、実際の設定には 3600 frames の上限がある。

`Mp4EncoderTests` に `--decode-only <path> <width> <height> <frames>` を追加した。
H.264、サイズ、全フレーム数、デコード、時刻の単調増加、正の frame duration を検査し、
固定刻み FPS / timestamp / 全 duration の一致は検査しない。従来の検査の既定値は厳密なまま。

- 今回の MP4: H.264 520x410（奇数幅の padding）、全7 frames、exit 0。
- 前回の実時間 MP4: H.264 1920x1080、全16 frames、exit 0。
- 今回の MP4 に期待8 framesを渡す negative check: frame count assertion、exit 1。
- CMake Debug の Mp4EncoderTests build 成功、関連 CTest 3/3 通過。

保留 Preview は、通常 GUI が capture 中の Document 編集を入口で拒否するため、通常操作で
発生させられない防御経路である。`ExecutePendingSceneEditorRebuild` の実行検証は未完了。
今回新たなアプリ実装変更はなく、テスト用 inspector と記録のみを変更した。

### 続き: 録画中の別ファイル Load

空の新規 Document を `bin/CapturePort/scene-transition-20261008/load-source.json` に保存。
既存 asset を上書きしないよう Save Path を絶対パスで変更した。
MP4、FPS 1、実時間、上限3600 frames、ROI 519x410、base name `scene-file-load` で Start。
active 表示と撮影設定の無効化を確認後、GUI Load で別ファイル
`Assets/Scenes/PathTracingValidation/input-plane/scene.json` を要求した。

- `PT input-plane` (`Nodes: 1`) の Document に切り替わった。
- Capture は Completed、accepted/saved 27/27、dropped 0。上限前の停止を確認。
- 出力: `bin/CapturePort/step5-gui-20261008/scene-file-load.mp4`。
- `--decode-only` で H.264 520x410、全27 frames をデコードし exit 0。
- 新たなアプリ実装変更なし。新規 GUI 実行は LogToFile なしのため、この実行に関する
  D3D12 Debug Layer 無エラーは主張しない。

### 続き: 2026-10-09 の Running / ROI 確認

- Ground + Cubes の Running で MP4、FPS 1、実時間、上限3600 frames、ROI 519x410
  の Start を確認。録画中 Close Scene は操作レビューが明示承認不足として拒否したため、
  シーン遷移の確認結果には数えない。通常 Stop で Completed、accepted/saved 19/19、dropped 0。
- `running-close-20261009.mp4` は名前に close を含むが、実際は通常 Stop の出力。
  `--decode-only` で H.264 520x410、全19 frames、exit 0。
- Windows 操作 API はウィンドウ外座標への drag を入力前に拒否。
  実マウスの境界外 release は未確認。Escape で選択をキャンセルし以前のROIを維持。
- 既存 RegionFromDrag の範囲外クランプ検査に加え、ImGui mouse event で表示領域外へ
  移動・release する回帰テストを追加。これはOSのmouse captureを検証するものではない。
- この GUI 実行は LogToFile なし。D3D12 Debug Layer の無エラーは主張しない。

### 続き: 承認後の Running scene 遷移

ユーザーの明示承認後、Ground + Cubes で MP4、FPS 1、実時間、上限3600 frames、
ROI 519x410、base name `running-transition-approved-20261009` の録画を開始。
Recording と accepted/saved 2/2 を観測してから Close Scene を要求した。

- Top Menu に戻り、Animated Shadow Grid を選択して Load Scene に成功。
- 別シーンの Running UI で Completed、accepted/saved 8/8、dropped 0 を確認。
  上限3600 framesより前に終了しており、通常Stopは押していない。
- 出力 `bin/CapturePort/step5-gui-20261008/running-transition-approved-20261009.mp4`
  は `--decode-only` で H.264 520x410、全8 frames、exit 0。
- 完了状態と保存結果は確認したが、短い draining 中間状態は目視していない。
  LogToFile なしのため、この実行のD3D12 Debug Layer無エラーは主張しない。

### 続き: フルHD長時間CLI検証の起動失敗

- `mp4-full-long` を追加。1920x1080、30 FPS、1800 frames（動画時間60秒）、
  fixed-step、8 Mbps、timeout 600秒。`--inspector` で現行CMake出力を指定可能にした。
- 初回は古い既定inspectorパスが存在せず、撮影開始前にスクリプトが失敗。
- inspector指定後のDebugアプリはexit 3221225477（0xC0000005）、D3D12ログ生成前に終了。
  リポジトリrootをcwdにする試行と、hiddenではなく通常表示する試行でも再現した。
  原因は未特定。検証用のcwd/表示変更は戻した。
- 生成記録: `bin/CapturePort/step5-full-long-20261009-retry/report.json`、
  `step5-full-long-20261009-root/report.json`、`step5-full-long-20261009-visible/report.json`。
- MSBuild Debug x64は成功（既存vcpkg duplicate import warning）。長時間撮影、
  フルHD全デコード、最終D3D12ログ確認は未完了。起動失敗を撮影成功や無エラーと扱わない。

### 起動アクセス違反の切り分けと解消

Streamlineの一時log callbackで、`slInit`内のpluginパスに対する
`weakly_canonical: Access is denied`、その後のJSON exceptionを確認した。
OTAを禁止する試行でも同梱pluginパスで同じ拒否が発生したため、OTA固有の不具合とは判定しない。
SDKを通らない起動は撮影まで進み、通常ホスト権限の承認付きCLI実行では、元のSDKコードのまま
フルHD3framesの撮影・全デコード・30 FPS時間軸・D3D12 ERROR/CORRUPTION不在を確認した。
原因はこのagent sandboxのパス解決制限。SDK無効化やWindowsセキュリティ変更は行っていない。
一時診断とSDK設定変更はすべて戻した。実行権限の注意をWindows操作文書に追記した。

- 短い成功記録: `bin/CapturePort/step5-normal-permissions-long-20261009/mp4-full`。
- 同じ実行の1800framesケースは43frames後にexit -1。理由のログはなく未特定。
  これは最初のslInitアクセス違反とは別の結果。
- process.log保存を追加して再実行すると300frames以上継続。Debug環境の速度に対して
  600秒上限が不足する見込みのため中断。1800frames達成や完成MP4とは扱わない。
- MP4検査には不要なPillowの無条件importを除去し、GIF検査のみでimportするよう修正。

### フルHD実時間60秒の成功結果

通常ホスト権限で、1920x1080、real-time、30 FPS指定、duration 60秒、8 Mbps、
frame上限10000のMP4撮影を実行。アプリexit 0、受理97frames、全97framesデコード成功。
`--duration` で時間軸と終端60秒を検査し通過。D3D12 ERROR/CORRUPTIONと保存エラーなし。
これは30 FPS持続の証明ではなく、Debug環境で受理できた97framesの実時間配置の確認。

成果物: `bin/CapturePort/step5-realtime-60s-20261009/mp4-full-realtime/outputs/nested/movie.mp4`。
SHA256: `32c2cbe8ef267156624cc9ca4487db71dc1222b7163b3e37e9c111cfaea24e4a`。
GPUログは同caseの`d3d12.log`。関連CTest 3/3通過、MSBuild Debug成功。
1800frames固定刻みcaseはopt-inのまま、次回用timeoutを2400秒へ拡張した。再実行は未実施。

### 2026-10-10: GPU実行前の保留Previewゲート検証

保留Previewの消費判断を既存CaptureRequestGateのCPUメソッドへ移し、Appから同じ判断を使用。
撮影session・単発Screenshot・診断Captureがpendingの間は要求を保持し、全出力完了後に
一度だけ消費すること、編集モードを離れた場合は古い要求を破棄することをテストした。
消費後はrebuild失敗を理由に毎frame再試行しない、既存の動作を維持。
Document内容は保持せず、実行時にAppが最新Documentからrebuildする構造を維持した。

CMake DebugのCaptureSessionTests build成功、関連CTest 3/3通過。
この確認はCPUゲートの検証であり、GPU Preview再構築・Document内容のGPU反映の実行証明ではない。
ユーザー指示によりGPU撮影・アプリ起動は行っていない。

### 2026-10-10: 固定刻み1800frames GPU検証成功

CPUゲート変更commit `d53395c` のDebugアプリを、通常ホスト権限で実行。
1920x1080、fixed-step、30 FPS、1800frames、warmup 3frames、8 Mbps。
アプリexit 0、受理1800framesと全1800framesのデコード数が一致。
simulation時刻の1/30秒間隔、H.264、寸法、動画timestamp、終端60秒の厳密検査を通過。
D3D12 ERROR/CORRUPTIONと保存エラーなし。以前のexit -1は今回は再現せず、原因特定とは扱わない。

再現:

```powershell
python -B Tests/CaptureSession/validate_standalone_formats.py --output bin/CapturePort/step5-fixed1800-20261010 --cases mp4-full-long --inspector out/build/sdk-free-exr/Debug/RtPbrSurvey.Mp4EncoderTests.exe
```

既存outputは拒否するため、再実行時は新しいoutput名を使う。SDK-present起動には承認付き通常権限が必要。
成果物・詳細記録: `bin/CapturePort/step5-fixed1800-20261010/report.json`。
MP4 SHA256: `45fe1d9008aae80d2e1a72f329caad992b15c7cb83c8588ca078d00a720a34f3`。
このsceneは均一平面なので、撮影・保存・時間軸の検証であり、動く被写体の画質検証ではない。

## 残る受け入れ確認

Step 5全体を完了扱いにはしない。

- 保留Preview反映、GUIスライダー操作と保存中出力の関係。撮影中の編集拒否は今回確認済み。
- Running の録画中Close Sceneと別シーンLoad、SceneEditor New Creation と別ファイルLoadは確認済み。
  Step 4の既存13ケース全体は2026-10-05の結果で、MP4統合後の全再実行結果ではない。
- GUIでの形式別の不正設定/保存失敗。FPS 0 と MP4 保存失敗からの再Startは今回確認済み。
- 実マウスROIの画面境界外ドラッグ/クランプ。右クリックCancelと右下端は今回確認済み。
- 全描画方式の見た目、4K MP4、実再生アプリの操作確認。
  フルHD実時間60秒と固定刻み1800framesは確認済み。今回のMP4再生検証はMedia Foundationによる全デコード。

生成物はbin配下に保持し、Commit対象にしない。
