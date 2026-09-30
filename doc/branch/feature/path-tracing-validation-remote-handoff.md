# Path Tracer validation: 別PC / 別Codexへの作業依頼

作成日: 2026-10-01

## 目的

RtPbrSurveyのPath Tracerについて、画像・数値・再現条件から完成度を判断できる検証基盤を整える。
Work-3側が推定器とshaderの修正を担当し、別PC側は独立した検証シーン、測定script、結果レポートを担当する。
検証で問題を見つけた場合は再現条件を報告し、推定器の変更はWork-3側と調整する。

最初にPart 1とPart 2を実施する。Part 3-5はそれぞれ独立した後続タスクとし、一度に実装しない。

## 開始条件と分担

- 最新のremote `main`から作業ブランチを作る。実際のbase commit、branch、workspaceを記録する。
- PR #82の複数光源PT検証とshadow ray修正、PR #83の非対称カメラ投影を含む状態を使用する。
- 別PCのworkspaceはそのPCで確認する。この文書のWork-3パスを編集先として流用しない。
- 主な編集先は`Assets/Scenes/PathTracingValidation/`、`Tests/PathTracing/`、`doc/branch/feature/`。
- `Shaders/`、`Engine/`、`Renderer/`、光源実装、camera API、Capture Session APIの変更が必要になったら、
  まず最小の再現と変更案を報告する。検証のための仕様変更を無断で混ぜない。
- 既存のcapture、PFM reader、ROI、統計処理を調べ、使える部分を再利用する。
- shader修正中の別PCとの比較は、同じcommit・設定・scene hashで実施する。

## 共通の検証条件

ToneMap前のlinear HDRを主要な数値評価に使う。PNGは構図と目視確認に使う。
既存PFM経路を基準とし、EXRを利用する場合はfloat形式、色空間、上下方向、ROI、channelの解釈を確認する。
UI overlay、exposure、ToneMap、背景の違いで推定器の差を判断しない。

各runはscene、camera、render preset、resolution、seed、sample数、bounce数、環境sampling mode、
Russian roulette、shadow設定、capture frame、ROIを明示する。自動露出やcamera移動は静止比較では無効にする。
固定seedの再実行、異なるseedの変動、非有限値、process failure、timeout、D3D12 errorを検査する。
GPU間のbitwise一致は要求しない。同一環境の固定seed再現性と、GPU間の統計的な差を別々に報告する。

解析式との比較と有限sample参照画像との比較は区別する。高sample画像はground truthと呼ばず、
独立seed間の不一致や標準誤差を併記する。閾値は結果を見る前に根拠を決め、都合よく緩めない。
失敗runもレポートに残し、再実行成功だけで置き換えない。

## Part 1: 解析用テストシーン

### 作業

小さく、照明とmaterialの条件を説明できるscene JSONとrender presetを追加する。

1. 一定環境下の平面または球: 拡散反射と環境samplingの基礎比較。
2. 単一光源と遮蔽物: visibilityと直接光を切り分ける。
3. 2面の間接反射: bounce数による寄与とcolor bleedingを確認する。
4. roughnessの異なる面: GGXを含むsamplingの収束比較。

各sceneはcameraとROIを固定し、対象以外の照明、emissive、環境、背景の寄与を明示する。
不要な大規模assetや外部downloadを必要としない構成を優先する。

Lambertの解析期待値を使う場合は、実際のshaderのBRDFがそのモデルと一致するか確認する。
metallic=0でもspecular項が残る実装では、単純な`albedo * environment radiance`を全出力の期待値にしない。
必要なら実装と同じBRDFの独立CPU積分を参照とし、数式・仮定・数値積分誤差を記録する。
既存のmaterial APIで純Lambertを表せなければ、その制限を報告してsampling比較から着手する。

### 完了条件

- scene/presetと実行手順がcommitされ、source assetを変更せず再実行できる。
- sceneごとに検証対象、期待される関係、未検証の範囲が説明されている。
- 少なくとも1件は独立した数値期待値との比較、1件は遮蔽による寄与の切り分けを実施する。
- Debug buildのcaptureでD3D12 errorがない。数値期待値を満たせない場合は再現付きで報告する。

## Part 2: 収束レポート

### 作業

`compare_hdr.py`、`compare_convergence.py`、`test_compare_hdr.py`を読み、Part 1のsceneで使えるよう拡張する。
低・中・高sample数を共通の独立参照へ比較し、複数seedで集計する。
最初は短いsmoke runを行い、その後にGPU性能に合わせた本測定のsample数を決める。

- RGB RMSE、平均RGB、seed間標本分散、mean-image誤差を出力する。
- 参照seed間の不一致と、使用したseed数を出力する。
- sample数に対する誤差・分散を図にし、機械可読JSONと簡潔なMDを生成する。
- 環境BSDF / NEE / MIS、直接光のみ / 間接光ありを明示して比較する。
- bounce上限を固定した比較と、bounce上限による変化を混同しない。
- scriptの変更には、ROI・sample正規化・指標計算などの意味のある小さなテストを付ける。

### 完了条件

- Part 1の少なくとも2sceneで複数sample数・複数seedの比較が再実行できる。
- 同じreferenceを使ってsample数による傾向を比較できる。
- 観測された収束傾向と、reference不足などで判断できない点が記録されている。
- 1つのGPUやsceneでの結果を一般的な優劣として結論付けない。

## Part 3: シーンスケールと自己交差

同じ相対構図を小・中・大スケールで作り、normal bias、ray TMin/TMax、接触影、光漏れを測る。
スケール変更ではcamera、geometry、光源位置、rangeを揃える。
Point/Spotの距離減衰により入射照明が変わる場合は、その変化を計算するか、照明条件を保つ強度補正を明示する。

遮蔽物なし・光源手前・光源直後、正面・斜め配置を含める。
既存`Test-LocalLightVisibility.ps1`のnear-light検証を再利用する。
成功条件は、期待する遮蔽関係を維持し、biasによる接触影の浮きや自己交差を定量・画像で説明できること。
固定biasから別方式へ変更する提案は、測定結果を根拠にWork-3側へ渡す。

## Part 4: 性能測定

resolution、samples/frame、bounce数、light数、geometry量を一つずつ変え、PathTracingPassのGPU時間を測る。
warm-up、測定frame数、medianと上位percentile、GPU/driver、build、電源条件を記録する。
GPU時間が取得できないrunをCPU FPSで代替して同じ指標として扱わない。

accumulation clear、TLAS更新、環境分布構築、capture/readbackが測定へ混入する条件を説明する。
結果はCSV/JSONと図で出し、ボトルネック候補と次の測定案を報告する。
性能最適化のshader変更はこのタスクに含めず、測定後に別の作業として調整する。

## Part 5: PT入力バッファの検証

既知のgeometryとcamera移動を使い、次のresourceを確認する。

- `PathTracing.NormalRoughness`: normalの座標系、向き、roughnessの値。
- `PathTracing.ViewZ`: 深度の定義と既知距離との対応。
- `PathTracing.MotionVectors`: 静止時、camera移動時、可能ならobject移動時の符号と単位。
- `PathTracing.Albedo`: material値、texture、色空間との対応。

Debug Texture Previewの表示変換と、resourceの保存値を区別する。
非対称カメラ投影でもprimary hitとmotion vectorの整合性を検証する。
既存のmotion vector capture scriptを利用し、必要な追加条件を小さく実装する。
成功条件は各bufferの定義・既知値・観測値・許容誤差を説明できること。
denoiser、NRD、DLSS RRへの接続は、この検証結果を受けて別途設計する。

## 成果物とcommit先

- scene/preset: `Assets/Scenes/PathTracingValidation/`。
- scriptと計算テスト: `Tests/PathTracing/`。共通scriptを変更するときは既存の使い方を維持する。
- sceneと指標の説明: `Tests/PathTracing/README.md`から参照できるMD。
- 最終報告: `doc/branch/feature/path-tracing-validation-remote-report.md`。
- 各Partの結果要約: `doc/branch/feature/path-tracing-validation-results/part-N-summary.json`。

結果要約JSONにはbase/tested commit、workspace、branch、GPU、driver、build、実行command、
scene/preset hash、sample/seed/ROI、指標、判定、生成artifactの相対pathとhashを含める。
数値データ量が大きい場合は代表指標と完全レポートの場所をcommitする。

PFM/EXR/PNG、動画、raw log、大量の測定JSONは`bin/PathTracingValidation/`などのignored出力先に保存する。
`bin/`、`obj/`、`build/`、`packages/`、`.vs/`、`.vscode/`、`sl.log`をcommitしない。
通常は画像そのものをcommitせず、レビュー用画像はPR添付または利用可能な共有場所へ置く。
共有ができない場合も、artifactの場所と再生成commandは必ず報告する。

## 実施と引き継ぎ

1. 最新main、GPU、driver、toolchain、既存scriptの起動を確認し、短い開始報告を出す。
2. Part 1を小さなcommitで実装し、sceneと期待値のレビュー材料を出す。
3. Part 2を実装し、smoke run後に本測定を行う。
4. 不具合を検出したら、最小scene、command、設定、数値、画像の場所を先に報告する。
5. Part 1+2のPRを作成し、以降のPartは別ブランチで一つずつ進める。

commit/push/PRは担当PCのユーザーから許可された範囲で行う。mainへの直接push、履歴rewrite、
他担当の変更の取り消しは行わない。PRのmergeはユーザーの指示に従う。
textはCRLF、UTF-8 without BOMとし、repoのAGENTS.mdと既存styleを守る。

最終報告には、実施Part、変更ファイル、commit、build/test、主要指標、判定できたこと、
残る疑問、次の作業案を記載する。末尾に機械可読な`Status: done`または`Status: blocked`を付ける。
`done`はそのPartの完了条件を満たした場合に使い、不具合の発見を修正完了と表現しない。
