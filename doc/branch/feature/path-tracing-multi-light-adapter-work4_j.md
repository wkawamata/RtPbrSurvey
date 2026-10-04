# PT複数光源adapter: Work4検証

## 位置付け

- 編集workspace: `C:\work\RtPbrSurvey-work-4`。Work3と#02の作業ブランチは編集していない。
- 作業ブランチ: `codex/pt-multi-light-adapter-work4`。
- #02のNEE/MIS基点: `origin/codex/path-tracing-nee-mis` `ee28c8b`。Raster/Hybrid複数光源が入ったmain `da92ed9` をWork4ブランチへ統合した。#02自体は2026-09-26時点でmain未統合。
- 依存先の`PathTracingLightSample`と`PathTracingDirectLightCandidate`契約を維持し、#02のBRDF、environment NEE、MIS weight、seed生成は変更していない。

## 実装

- `DirectLightEvaluation.hlsli`へDirectional/Point/Spotの評価式を抽出した。Raster/HybridとPTが同じ方向、距離減衰、range window、spot coneを使用する。
- PTは既存のper-frame LightingConstants (b2)の灯数・配列を読む。HLSL reflectionで`ptLightCount` offset 120、`ptLights` offset 128、CB2を確認した。CPU側`LightingConstants`のstatic_assertと一致。
- PTは有効な各灯からdelta sampleを1件ずつ生成して直接光を全列挙する。全列挙のため各candidateの`selectionPdf`は1とし、灯数で割ったり逆数を重複適用したりしない。Point/Spotのshadow ray上限は光源までの有限距離（rendererの`rayTMax`で上限）とする。距離が`rayTMin`以下、放射輝度0、無効灯はsampleを無効化する。
- 主Directionalの影指定IDはRaster/Hybridの制御として残し、PTの光源列挙には使用しない。灯数・型・有効状態の変更時は既存`SetLightingParams`からPT accumulationをLighting理由で無効化する。
- `-SceneFile`起動時にも`-EnablePathTracing`のCLI設定を適用する。これが無いと自動撮影はRasterのままでPT sample targetに到達しない。
- #02のPFM screenshot source選択をmainの`ScreenshotRequestQueue`へ適合させた。PNG/PFM混在順のテストを追加した。

## 検証

- Debug x64アプリビルド成功。既存vcpkg MSB4011 warning 1件、error 0。CMake ALL_BUILD成功。
- `SceneRendererSettings`、`SceneDocument`、`SceneDocumentBuilder`、`RenderPresetStore`、`Screenshot`、`ScreenshotRequestQueue`のCTest 6/6成功。
- RTX 2080 Ti / 1920x1080 / seed 1 / 1 sample / 直接光のみの同梱Multi-Light Validationシーンで、mixed、Directionalのみ、Pointのみ、Spotのみ、全灯なし、Point影OFFを撮影。各runでD3D12 ERROR 0、既知buffer initial-state WARNING 4件。
- mixedと主影ID=0のPNGはSHA256完全一致。mixedの再撮影も完全一致: `D34C80AD5D6A9A7E2FCAB00246A7FC8F643B4E2A7288C376E9402DB748F81C5A`。Pointのみ、Spotのみ、全灯なしはそれぞれ異なる画像。Point影ON/OFFも異なり、影OFFでは球の落影が消えることを目視確認。
- #02元実装`ee28c8b`を別worktreeでビルドし、DamagedHelmet / scene defaults / seed 1 / 1 sampleを現ブランチと同条件で撮影。単一Directional PNGはSHA256完全一致: `DBE5AC6FD8E0DAAF37B3A5DE0F9BDAA6F64E844BA38CBCE591B16577FF572C30`。両runでD3D12 ERROR 0、既知WARNING 2件。
- 共通光源評価式の抽出後、Raster/Hybrid混合灯fixtureの120-frameキャプチャはPR #74時の画像とSHA256完全一致: `04A335B61E486019888E7D54D25048272A6992B9D199598D1E1EFD347208C63D`。
- mixed 1 sampleのPFMを保存。1920x1080のlinear HDR RGBがすべてfiniteで、D3D12 ERROR 0。
- 4灯 + environment importance MIS mode 7 / seed 7 / maxBounces 2 / 4 samplesのPFMも保存。`accumulatedSamples=4`、environmentSamplingMode=7、全HDR値finite、D3D12 ERROR 0。観測したPathTracingPass GPU平均は約9.33 ms（有効3 frame）。単一条件の値であり、性能保証ではない。

生成画像、PFM、ログ、一時preset、撮影スクリプトは`bin/x64/Debug/PathTracingMultiLightWork4`配下に置き、commitしない。baseline worktreeはWork4内の`build/pt-nee-baseline`、ビルドログは`C:\work\RtPbrSurvey-agents\pt-multi-light-work4-*`。

## 残る確認

- 編集直後の最初のPT frameのaccumulation resetとhistory clearは、2026-09-27にUI操作と一時計測ログで確認済み。下記の追補を参照。
- Point/Spotの光源直近や遮蔽物が光源の背後にある極端な配置については専用シーンでの境界テストを追加できる。現検証は同梱fixtureでの有限距離・影ON/OFFの確認。
- PTは各bounceで最大16灯を全列挙する。灯数が多い場合のGPUコストと、必要なら離散的な光源選択PDFを導入する性能検討は後続。
- #02のNEE/MISはPR #75、Work4 adapterはPR #76としてmainへ統合済み。#02本体の推定量変更とWork4のadapter差分は独立PRのまま維持した。
## 2026-09-27: UI光源編集直後の蓄積リセット実測

PR #75 / #76はmainへマージ済み。この検証はWork4の同一実装（2e8752d）を使用した。

- Multi-Light Validationの一時コピーをScene Editorから読み込み、PT / accumulate=true / 1 sample per frameで実行した。
- Engineに一時ログを入れ、UI編集前後、履歴UAV clearコマンド記録後、PT dispatch直前、frame commit後を記録した。計測コードは検証後に取り除いた。
- UI操作はWindows Computer Useから行い、光源設定の保存は行っていない。

| UI操作 | 編集直前の蓄積数 | reset直後 | 最初のdispatch | 最初のcommit |
| --- | ---: | --- | --- | --- |
| Directional 1: Enabled OFF | 2803 | count/index=0, valid=false, clear=true | count/index=0, valid=false, clear=false | count/index=1, valid=true |
| Point 3: Intensity 8から4 | 2803 | count/index=0, valid=false, clear=true | count/index=0, valid=false, clear=false | count/index=1, valid=true |
| Spot 4: Enabled OFF | 1687 | count/index=0, valid=false, clear=true | count/index=0, valid=false, clear=false | count/index=1, valid=true |

全ケースでreset reasonはLighting（5）。reset後、最初のdispatchより前にhistory clearが記録され、次フレームではcount/index=2へ進んだ。古い蓄積数を引き継いだdispatchは観測されなかった。

観測対象はCPU側の状態とGPUコマンド記録順であり、GPUの履歴テクスチャreadbackや編集直後の画像のピクセル比較は実施していない。起動直後のフレームではなく、蓄積が進んだ状態でのUI光源変更を測った。

一時ログ: `build/pt-ui-reset-trace.log`（非commit）。各操作の開始tickは984579281、984626000、984654109。検証用scene/presetと元ソースのbackupも`build/`配下に保存した。

Status: UI-triggered PT accumulation reset and history-clear command ordering verified.
