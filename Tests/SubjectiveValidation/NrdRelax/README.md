# NRD RELAX 目視A/B確認

数値ゲートとは別の、人が画面を見て判断する確認です。結果は初期状態ではすべて
`unanswered` です。起動成功や数値テスト成功から、目視合格を推定しません。

## 起動

SDK診断ONのDebugビルドを使います。作業ディレクトリや元のシーン・プリセットを
変更せず、`bin/PathTracingValidation/` に専用のログ・記録欄を作成します。

```powershell
.\Tests\PathTracing\Start-NrdVisualReview.ps1 -Scene Disocclusion -Launch
```

`-Launch` なしでは準備だけ行い、アプリは起動しません。`-DryRun` は書き込みも
起動もせず、起動条件をJSONで表示します。`-InitialMode Native` でNative表示から
始められます。既定の実行ファイルは `build/Debug/RtPbrSurvey.exe` です。

## 最初の確認

1. DebugのPath Tracing設定を開きます。Debugが見えない場合は `F1` で表示します。
2. `Path Tracing Output = Radiance`、`Samples / Frame = 1`、
   `Pixel Center Primary = ON`、`Denoiser Distance Diagnostics = ON` を維持します。
3. `NRD RELAX Preview (Diagnostic)` がONのとき、`PT Presentation` が
   `NRD RELAX diagnostic (per-pixel Native fallback)` であることを確認します。
4. OFFのときは `Native PT` です。チェックが無効、fallback表示、NRD Failure表示なら
   目視比較は成立していないので、合格にせず状態を報告してください。

一般のNRD Support表示は、まだ正式統合前の状態を示す場合があります。この手順は
明示的な診断プレビューの確認であり、Available/default-on化ではありません。

## Disocclusion の確認

描画領域で左ドラッグして、小さくゆっくり左右に回り込みます。TabでFreeLookへ
切り替える必要はありません。ドラッグ開始位置はImGuiのウィンドウ外にします。

1. 静止状態でNRDをON/OFFし、ノイズ低減と過度のぼけを比較します。
2. 左右に動かし、赤い板の後ろから出てくる緑の面の色・輪郭を見ます。
3. 同じくらいの速さで移動方向を反転し、二重輪郭や引きずりを見ます。
4. 操作を止め、直後から約1秒間のちらつき・残像・落ち着き方を見ます。
5. NRD表示だけを切り替え、フリーズや予期しない蓄積リセットがないか見ます。

この起動方法は `-NrdContinuousCameraHistory` を明示します。通常GUI起動の
カメラ編集はNRD履歴をリセットするため、そちらと混同しないでください。
FOV変更・シーン編集・手動Resetは意図した履歴リセットなので、今回の移動比較では
行いません。動いているNativeは低spp、停止するとNativeも蓄積が進みます。
表示ON/OFFだけでは履歴を消さず、手動A/Bは完全に同じ時刻の同一ノイズ試料でも
ありません。厳密な再現比較は数値タイムライン側の結果と区別します。

## StableRadiance の確認

最初のアプリを閉じてから、別セッションで次を実行します。

```powershell
.\Tests\PathTracing\Start-NrdVisualReview.ps1 -Scene StableRadiance -Launch
```

NRDをON/OFFし、左の発光面、中央の青いUnlit、背景を見ます。中央が照明や隣の色で
変色しないこと、発光が消えたり二重に明るくなったりしないことを確認します。

## Glossy の確認

最初のアプリを閉じてから、別セッションで次を実行します。

```powershell
.\Tests\PathTracing\Start-NrdVisualReview.ps1 -Scene Glossy -Launch
```

対象は `16-nrd-glossy-reflections` の4球です。左上は非金属roughness 0.6、
右上は非金属0.1、左下は金属0.6、右下は金属0.1です。`Max Bounces = 4`、
Environment Map / Importance MIS、Skybox ONを維持します。2バウンスでは球同士の
映り込みの経路が打ち切られるため、今回の基準にはしません。

カメラを動かさず、NRDをON/OFFして次の3項目を確認します。

1. 明るいハイライトの形・輪郭・色が不自然に変わらないか。
2. 特に右上と右下の球に映る小さな球が、消えたり暗くなりすぎたりしないか。
3. 暗部の光漏れ、不自然な色、黒つぶれがないか。

Nativeは時間とともに蓄積します。ON/OFFは同一ノイズ試料や同一時刻の比較では
なく、30フレームの数値評価とも区別します。必要ならNativeが落ち着くまで待って
から形・色を比べてください。この確認は静止時のみで、移動時の合格は含みません。

最初の8192-spp参照による局所ROI評価は14項目中12項目が合格、2項目が参照収束不足
で保留でした。その後、同じ固定マスクを32768-spp参照で再評価し、全参照が収束条件を
満たしました。現在は13項目が合格、右上の非金属球の映り込み領域だけが平均RGBベクトル
バイアス8.305%で5%の基準に不合格です。暗部の保留は解消しました。原因が入力・合成・
履歴・SDKフィルタのどこかは未確定です。目視結果はこの数値判定を上書きせず、別の
証拠として記録します。

## 記録

起動ごとの出力ディレクトリに `session.json`、`review-preset.json`、`runtime.log`、
`results.json`、この手順のコピーを保存します。結果は `pass / fail / unable /
unanswered`、補足は日本語で記録できます。全体statusは手動確認まで未回答のままです。
このチャットに各項目の結果と気になった場所を返してもらっても構いません。

このシーンだけの目視合格を、金属・低roughness・動く物体・カメラカット・全シーンの
合格とは扱いません。生成ログや個人の評価記録はGit管理しません。
