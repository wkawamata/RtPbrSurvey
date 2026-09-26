# 複数光源: 実装・検証メモ

2026-09-21。workspace: `C:\work\RtPbrSurvey`。開始コミット: `8786df8`。
未コミットの実装差分。開始時の `Scene/SceneGraph.cpp` / `.h` の変更は保持し、本実装では編集していない。

## 実装

- `Shared/DirectLight.h`: Directional / Point / Spot、安定ID、最大16灯、入力検証、64-byte GPU構造体。
- `Shaders/DirectLights.hlsli`: 全Raster/Hybrid経路共通の方向・radiance・距離減衰・Spot円錐評価。
- フレーム別Lighting CBVを1280 bytesに拡張。lightCount offset=120、主Directional index=124、光源配列=128。CPUのstatic_assertとシェーダービルドで配置を確認。
- Deferredは各光源の直接光を加算し、主DirectionalだけにShadow Maskを適用する。Forwardも同じ方向・減衰を使用する。ForwardのBRDFは既存の簡易拡散反射を維持し、影は未対応。
- Reflection Evaluate / Ray Hit Debugはレイ開始点のnormal biasと反射サンプリング方向を含めたヒット位置で全光源を評価する。IBL・Emissiveを灯数倍しない。
- `Ui/DirectLightUi.h`: 一覧、選択、追加、複製、削除、有効、名前、型、色、強度、位置、方向、range、円錐角、主Directional指定。Scene EditorではRender Preset > Lightsから編集し、Save Presetで保存する。
- 光源設定変更は既存のReflection history / PT accumulation無効化に接続。リソース全再生成を要求しない。
- renderer schema v4: `lighting.lights` / `primaryShadowLightId`。v1～v3の旧lightDirectionは符号反転・正規化して移行する。旧Deferredと同じsurface-to-light方向を保持し、旧Forwardとの符号不一致を解消する。
- 空配列は0灯として保持。全体Direct Light、IBL、Emissiveは独立。主Directionalを失った場合は影を無効にする。
- 追加提案・将来の所有モデルは `multi-light-other-pc-task_j.md` の2026-09-21追記に記録。

## 実施済みの検証

- [x] Debug x64ビルド、および変更対象VS/PSのDXCコンパイル。
- [x] SceneRendererSettingsTests / RenderPresetStoreTests。
- [x] 保存・復元のCPUテスト: 16灯混在、ID・値・無効状態、不正JSON、未知version/type、重複/負/小数ID、不正方向、負色・強度、range、円錐角、17灯拒否、空配列、旧設定移行、失敗時の設定保持。
- [x] 外部モデル不要の `Assets/Scenes/MultiLightValidation/scene.json` と4灯プリセット。
- [x] 混在、1灯、16灯、0灯、全無効、Forward、Hybrid Reflection、Point/Spotのみを1920×1080、120 warm-up framesでキャプチャ。
- [x] 0灯と全無効のPNG SHA256一致: `B34C2EDBEA3FDDC361A9B90EBA717FD44BA134A883F1A579FFC2D6C26F6B7832`。
- [x] Spot単独で下向き・上向き・range=0.1・円錐半角5/10度を比較。上向きとrange外は0灯と同じPNG SHA256。下向きで照射と外周の減衰、円錐を狭めたときの照射範囲縮小を目視確認。
- [x] 同一の新実行ファイルでv3旧単一光源形式とv4配列形式を比較し、PNG SHA256一致: `F82D0F0CD3A44AFA7CCA6DCE90E5EAFA6A9183B9FFE718272334B57C61923735`。これは移行の等価性確認であり、変更前実行ファイルとの比較ではない。
- [x] 上記各実行ログにD3D12 `[ERROR]` なし。各実行で同じ `[WARNING]` 4件: CreateCommittedResourceのbuffer初期状態UNORDERED_ACCESSがCOMMONとして扱われる通知。今回の光源CBVはupload bufferであり、この警告の対象ではない。変更前実行ファイルとのwarning件数比較は未実施。

### CPU計測

同一PC・1920×1080・Deferred・同一camera/exposure。各値は30/60/90/120フレーム時点のCPUフレーム時間(ms)。表示同期条件を変えていないため約16msに制約され、GPUコストの差を評価する測定ではない。

| 灯数 | CPUフレーム時間 (ms) |
| --- | --- |
| 1 | 15.56 / 15.96 / 15.84 / 15.47 |
| 4 | 15.92 / 15.80 / 15.65 / 16.11 |
| 16 | 15.82 / 16.09 / 15.56 / 15.09 |

### キャプチャ手順と成果物

```powershell
New-Item -ItemType Directory -Force bin\x64\Debug\MultiLightValidation
.\bin\x64\Debug\RtPbrSurvey.exe -SceneFile Assets\Scenes\MultiLightValidation\scene.json -CapturePath bin\x64\Debug\MultiLightValidation\mixed.png -CaptureAfterFrames 120 -ExitAfterCapture -LogToFile bin\x64\Debug\MultiLightValidation\mixed.log -LogFPS 30
```

生成画像・ログ・一時variantプリセットは `C:\work\RtPbrSurvey\bin\x64\Debug\MultiLightValidation`。commit対象外。
主な画像は `final.png`、`direct-only.png`、`forward.png`、`reflection.png`、`point-spot.png`、`spot-down.png`、`spot-up.png`、`spot-outside.png`、`spot-narrow.png`、`legacy-one.png`。初回の `mixed.png` / `mixed-verified.png` はIBL/Emissive有効の探索用画像であり、現在の検証プリセットは両方OFF。
ビルドログは `obj/multi-light-build*.log`、CPUテストビルドログは `build/scene-document-tests/multi-light-tests-build*.log`。
他PCへのタスク送信・repo外の協調用taskフォルダ作成は行っていない。

## 未完了・既知制限

- [ ] PT複数灯adapter: 現checkoutに指定のNEE/MIS契約がない。PTは主Directionalだけの暫定接続。Point/Spotの有限長visibilityを含め、契約統合後に接続する。PT本体と契約の重複定義は変更していない。
- [ ] 変更前実行ファイルと旧1灯Deferred画像の定量比較。移行方向・色・強度はCPUテスト済みだが画像互換の完了とは扱わない。
- [ ] Spot境界の数値精度・連続性の定量測定。向き・range外・円錐範囲変更の比較画像は取得済み。
- [ ] UI操作による追加・削除・型変更・保存再読込、および動的なReflection/PT履歴resetの操作検証。
- [ ] シーンノード所有、gizmo、光源編集Undo/Redo。段階1はrenderer presetに保存する暫定統合。
- [ ] 追加光源の影。Deferredは主Directionalだけ、Forwardは影なし、Reflectionヒット位置の全光源遮蔽は次段階。

段階1全体はPT統合等の未完了項目があるため完了扱いにしない。

## 2026-09-22: GPU timestamp計測

`-LogToFile` と `-LogFPS N` の併用時に、既存GpuWorkMeterの最新完了フレームのtotalとパス間timestamp差を併記するようにした。CPUのFrame番号はログ出力タイミングであり、同じ番号のGPUフレームを示さない。totalはStartGpu～EndGpuの計測区間で、Present待ちやCPU処理を含むwall-clock frame timeではない。

Debug x64再ビルド成功。既存シーン、1920×1080、同一camera/exposure、Deferred、IBL/Emissive OFF、主Directionalの影ON。1灯はfixtureの先頭Directional、4灯はfixtureそのまま、16灯は4灯を同じ位置・強度で4組に複製しIDを1～16にしたもの。灯種構成も変わる比較であり、同一灯種だけのスケーリング試験ではない。

各条件を順次1回起動し720フレームまで描画、10フレーム間隔でログ出力。最初の120フレームを除外し、出力フレーム130～720の60サンプルを集計した。

| 灯数 | LightPass中央値 (ms) | GPU計測区間total中央値 (ms) | LightPass平均 (ms) | total平均 (ms) |
| --- | --- | --- | --- | --- |
| 1 | 0.116736 | 1.742848 | 0.117555 | 1.815211 |
| 4 | 0.160768 | 1.789440 | 0.160717 | 1.887881 |
| 16 | 0.381952 | 2.006016 | 0.431309 | 2.172843 |

16灯/1灯の中央値比はLightPass約3.27倍、total約1.15倍。LightPassの増加は約0.265ms。このシーンでは描画全体が16倍にはならない。ただし16灯のtotal最大6.025216ms、LightPass最大1.843200msなど外れ値があり、単回実行の参考値。別解像度、画面占有率、灯種、電力・クロック条件や他プロセスのGPU使用で変わる。性能保証やRelease性能の結論にはしない。

PC搭載GPUはNVIDIA GeForce RTX 3080 Laptop GPU (driver 32.0.16.1664)とIntel UHD Graphics。WMIの搭載情報であり、今回のログには選択adapter名を記録していない。厳密な再計測ではadapter名・電力条件を記録し、実行順を反転した複数回の測定も行う。

実行例（variantはone / direct-only / sixteen）:

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe -SceneFile Assets\Scenes\MultiLightValidation\scene.json -RenderPreset bin\x64\Debug\MultiLightValidation\sixteen.json -CapturePath bin\x64\Debug\MultiLightValidation\gpu-sixteen.png -CaptureAfterFrames 720 -ExitAfterCapture -LogToFile bin\x64\Debug\MultiLightValidation\gpu-sixteen.log -LogFPS 10
```

3実行とも正常終了、D3D12 ERROR=0、既出のbuffer初期状態WARNING=4。生成物は前述の無視対象ディレクトリ内の `gpu-{variant}.log` / `.png`。PT統合・UI実操作などの未完了項目は上記のまま。

## 2026-09-25: リモート引き渡しと残検証

ユーザーの「全体をremote branchにPush」に従い、`codex/multi-light-scene-editor` に現作業差分を引き渡す。開始時から存在した `Scene/SceneGraph.cpp` / `.h` のworld transform設定追加も、内容を変更せず独立コミットとして含める。本書冒頭の「本実装では編集していない」は作業由来の説明であり、今回のブランチから除外する意味ではない。ログ、ビルド成果物、検証画像は含めない。

### Raster / Hybrid先行PRのマージ前に必要な検証

- [ ] Scene Editor > Render Preset > Lightsで追加、複製、削除、型変更、有効切替を実操作し、表示と描画が一致すること。検証用コピーを用いる。
- [ ] Save Preset → Reload Presetおよびアプリ再起動後のLoadで、ID、名前、型、値、有効状態、主Directional指定を維持すること。Scene DocumentのSaveとは別保存であることも確認する。
- [ ] 主Directionalを削除・無効化・Point/Spotに変更した後、古い影が残らず追加灯に主光源のShadow Maskが適用されないこと。
- [ ] 光源編集がForward / Deferred / Hybrid Reflectionに反映され、Reflection履歴がリセットされること。PTは暫定対応の主Directional変更について蓄積リセットを確認する。
- [ ] 変更前の単一光源Deferred画像を同じcamera / exposureで比較する。実施済みのv3/v4移行画像一致とは区別する。
- [ ] 最新remote mainとの差分・競合を確認する。今回のPush自体ではmainの取り込み・マージは行わない。
- [ ] マージ候補の状態でDebug x64ビルド、SceneRendererSettingsTests、RenderPresetStoreTestsを再実行し、混在灯のD3D12 ERRORがないこと。既知WARNINGとの比較も記録する。
- [ ] 同梱したSceneGraphのworld transform設定変更を別途レビューする。親あり/なし、非可逆親、不明ID、TRS分解失敗時の状態保持を確認する。未検証なら先行PRから分離する。

### 後続作業（先行PRの完了とは区別）

- [ ] #02のNEE/MIS契約統合後にPT複数灯adapterを接続し、Point / Spotの有限距離visibilityと全灯編集時の蓄積リセットを検証する。現在は主Directionalだけの暫定接続。
- [ ] Spot境界の数値精度・連続性を定量検証する。
- [ ] GPU測定はadapter名・電力条件を記録し、順序反転・反復で確認する。既存の単回測定を性能保証としない。
- [ ] シーンノード所有、gizmo、Undo/Redo、追加灯の影は別段階で設計・実装する。

他PCは `Shared/DirectLight.h`、`Runtime/DirectLightJson.h`、`Shaders/DirectLights.hlsli` を光源データと変換の入口として参照できる。CPU方向は光の進行方向、シェーダー評価時にはsurface-to-lightへ変換する。renderer schemaはv4、最大16灯。PT契約の重複定義は行わない。

Pushは引き渡しであり、上記未検証項目や段階1全体の完了を意味しない。

Status: blocked

## 2026-09-26: Work-4での引き継ぎ検証

### 作業状態と範囲

- 編集workspace: `C:\work\RtPbrSurvey-work-4`。Work-3は編集していない。
- 作業ブランチ: `codex/multi-light-validation-work4`。
- 引き継ぎ元: `origin/codex/multi-light-scene-editor` の `1df79b9`。
- main `5dd7789` を通常マージした `b010575` を検証基点とした。競合は依頼書MDのadd/addだけで、複数光源側の追記を保持して解消した。
- `e856cd4` の `SetSceneNodeWorldTransform` 追加は光源実装から参照されていない。単独のテストがなく、入力行列のfinite/TRS検証・失敗時の状態保持を含む別レビューが必要なため、`Scene/SceneGraph.cpp` / `.h` をmainと同じ内容に戻し、光源の先行差分から分離した。元実装は引き継ぎ元ブランチのコミットに残る。
- `PathTracingSampling.hlsli` / `shaders_PathTracing.hlsl` / NEE・MIS estimatorは変更していない。今回の検証追加はpushしていない。

### ビルド・CPUテスト

- [x] 引き継ぎ＋main統合後のDebug x64ビルド成功（ERROR 0、既存のvcpkg MSB4011 WARNING 1）。
- [x] CMakeのDebugテストビルド成功。
- [x] SceneRendererSettings / RenderPresetStore / SceneDocument / SceneDocumentBuilderの4テスト成功。
- [x] SceneGraph分離後もDebug x64ビルド成功（ERROR 0、既存MSB4011 WARNING 1）、テストビルド成功（WARNING / ERROR 0）、上記4テスト再実行成功。

NuGetは既存の `C:\work\RtPbrSurvey-work\tools\nuget.exe` を実行ファイルとして使用し、復元先はWork-4のpackagesとした。CMakeの依存prefixは `C:/work/RtPbrSurvey-work-4/vcpkg_installed/x64-windows/x64-windows`。

```powershell
ctest --test-dir build/multi-light-validation -C Debug -R 'SceneRendererSettings|RenderPresetStore|SceneDocument' --output-on-failure
```

ビルドのログフォルダ（編集workspaceとは別）:

- `C:\work\RtPbrSurvey-agents\multi-light-work4-debug-20260926`
- `C:\work\RtPbrSurvey-agents\multi-light-work4-tests-20260926`
- `C:\work\RtPbrSurvey-agents\multi-light-work4-baseline-20260926`
- `C:\work\RtPbrSurvey-agents\multi-light-work4-final-debug-20260926`
- `C:\work\RtPbrSurvey-agents\multi-light-work4-final-tests-20260926`

### キャプチャと画像互換

成果物のrootは `C:\work\RtPbrSurvey-work-4\bin\x64\Debug\MultiLightValidationWork4`。PNG、log、一時preset、`run-captures.ps1` は生成物でcommit対象外。

1920×1080、120 warm-up frames、同梱検証シーン、同一camera/exposureで比較した。各実行のD3D12 ERRORは0。WARNINGは各4件で、旧mainでも同じbuffer初期状態UNORDERED_ACCESSをCOMMONとして扱う通知4件を確認した。

| ケース | 結果 |
| --- | --- |
| mixed / forward / hybrid | それぞれキャプチャ成功。hybridはcontributionEnabled=trueを指定 |
| deleted-stale / deleted-clear | 主Directional削除後、古い主ID=1と解除ID=0でPNG完全一致 |
| disabled-stale / disabled-clear | 主Directional無効化後、古い主ID=1と解除ID=0でPNG完全一致 |
| point-stale / point-clear | 主DirectionalをPointへ変更後、古い主ID=1と解除ID=0でPNG完全一致 |
| one / legacy | 現実装のv4単一灯とv3旧形式でPNG完全一致 |
| baseline-legacy / one | 変更前main実行ファイルと現実装の単一灯でPNG完全一致 |

主光源ID残留テストは起動時の状態を比較したものであり、UIでの動的切替確認とは別の証拠である。

旧mainはWork-4内の `build/multi-light-baseline` に `5dd7789` のdetached worktreeを作ってDebugビルドした。同一のv3旧プリセットを `-RenderPreset` で指定して実行した。単なる新実行ファイル内のv3/v4比較ではない。baselineは元シーンJSONが参照するv4を読めないため、旧プリセットの明示指定が必要。

`baseline-legacy.png` / `one.png` / `legacy.png` のSHA256:

`F82D0F0CD3A44AFA7CCA6DCE90E5EAFA6A9183B9FFE718272334B57C61923735`

削除・無効化した4ケースのSHA256:

`64CCDD680EAA671AB123490B6662C1830998FE2F14038DAB034258725591894F`

Point型変更の2ケースのSHA256:

`E471999E0BE184F58C47DB0EFD854B8451B32AA1DD9FD767F13292DA748FB022`

### Scene Editorの実操作

同梱fixture自体を編集せず、成果物root配下の `ui/scene.json` と `ui/render-preset.json` のコピーを使用した。

- [x] Pointを追加（4→5灯）、複製（5→6灯）、複製した光源を削除（6→5灯）。
- [x] 追加PointをSpotに型変更し、無効化。型に合わせた編集項目の切替を確認。
- [x] Save Presetで保存したJSONを確認。id=5、name=Point 5、type=spot、enabled=falseと各値を保持。主ID=1を保持。
- [x] 未保存のSpotを追加後、Reload Presetで保存済み5灯へ復元。
- [x] アプリ終了・再起動後に同じScene DocumentをLoadし、保存済み5灯と無効状態を復元。主Directionalを選択すると主影指定のチェックも復元。
- [x] Save PresetはScene Documentを書き換えない。最初の保存直後に元scene.jsonとコピーのSHA256一致を確認（後述のcamera調整より前）。
- [x] 主Directionalを無効化すると主影指定が解除され、色付き追加灯だけの描画へ更新。
- [x] 主DirectionalをPointへ変更すると主影指定が解除され、描画も更新。
- [x] 保存状態へ戻した後、主Directionalを削除。Save Presetの結果は4灯、IDs=2,3,4,5、primaryShadowLightId=0。別光源への自動付替えなし。
- [x] Forwardへ切替後、青いPointの無効化で画面の青い寄与が消えることを確認。
- [x] Hybrid ReflectionのResolved Radiance表示（renderViewMode=32）、temporalHistoryWeight=0.9で、青いPointの無効化により反射内の青い寄与が消えることを確認。

Reflectionについて、`SetLightingParams` の配列・主ID変更検出から `InvalidateReflectionHistory` へ到達し、valid=false、temporal/sampling frame=0となるコード経路も確認した。ただしUI取得は変更後の画面確認であり、変更直後の最初の1フレームやGPU history clearを数値記録した検証ではない。PT accumulationの動的resetは今回の実操作対象外であり、完了扱いにしない。

`ui.log` は途中のscene再読込を含みWARNING 8件、`ui-resume.log` はWARNING 4件。いずれも同じ既知通知でERROR 0。

### 検証で見つかった別件と残作業

元fixtureをScene EditorでLoadすると、CLIでの同じシーンと異なりcameraが上を向き、対象が見えにくかった。UI検証用コピーだけposition.y / target.yを1.0に揃えると対象を表示できた。FreeLookへ渡すpitchの符号と、その後のcamera更新が調査候補。該当camera初期化コードは今回の光源差分では変更されておらず、本検証ではcamera実装を修正していない。恒久修正は別件として再現確認する。

Raster / Hybridの主要な未完了検証（UI、保存、影指定、旧実行ファイルとの比較）は上記で実施した。厳密な履歴切替フレームの計測、PT accumulation操作検証、PT複数灯adapterは引き続き残る。段階1全体を完了扱いにしない。

PT adapterの統合提案:

1. #02側のNEE/MIS契約が統合されたcommitを基点に、独立したadapter差分を作る。
2. `DirectLightSample` のsurfaceToLight / radiance / distanceを契約へ変換する。Directionalは既存の有限rayTMax、Point / Spotは光源距離をvisibilityの上限とする。
3. Directional / Point / Spotをdelta扱いとし、全列挙に余分なselectionPdfの逆数を掛けない。BRDF・visibility・MIS weightはadapterへ混ぜない。
4. 主影指定とPTの光源選択を分離する。現在の主Directionalのみの暫定bridgeを、契約に沿った全光源入力へ置き換える。
5. #02との合意後にのみPT本体へ接続し、灯数・型の変更と有限距離遮蔽、蓄積resetを別途検証する。
### 最終確認

SceneGraph分離後の実行ファイルで混合4灯を再キャプチャした。final-mixed.pngはmixed.pngとSHA256完全一致（04A335B61E486019888E7D54D25048272A6992B9D199598D1E1EFD347208C63D）。D3D12 ERROR 0、旧mainと同じWARNING 4件。

今回のRaster / Hybrid引き継ぎ検証は完了。PT統合と上記の追加検証は後続作業。検証メモおよびSceneGraph分離の差分は未commit・未push。
