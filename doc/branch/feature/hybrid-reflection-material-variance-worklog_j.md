# Hybrid Reflection Material Variance 作業ログ

英語版: [Hybrid Reflection Material Variance Work Log](hybrid-reflection-material-variance-worklog.md)。

このログはDamagedHelmetの特定texture領域に残る、面全体のstochastic variance診断を記録する。reflection signalとhistory semanticsは[Hybrid Reflection Contracts](hybrid-reflection-contracts.md)に維持する。

## 2026-08-14: Phase開始

- edge-stability phase完了後、`main`のmerge commit `a749d80`から`features/hybrid-reflection-material-variance`を開始した。
- 再現領域は、滑らかな頭頂panelに隣接する後頭部寄りsurfaceと、下面に露出するpipeである。
- DamagedHelmetは1つのprimitiveに1つのglTF material `Material_MR`を持つ。対象は別material IDではなくtextureで定義される領域として扱う。
- 症状はedgeだけでなく面の一部全体を覆い、camera停止後も見える。
- global defaultはstochastic sampling無効、temporal history weight `0.0`を維持する。reject画素近傍補助実験もdefault-offを維持する。

## 初期診断scope

1. visible roughness、metallic、normal、evaluated radiance、resolved radiance、temporal validityを同じviewで撮影する。
2. 2つの再現領域がroughness、metallic、normal-map周波数、hit/miss挙動、高輝度environment samplingのどれを共有するか確認する。
3. 持続するinput varianceとhistory rejectionを分離する。temporal validityがgreenでも、1 sample/frameの収束が遅ければvarianceは残り得る。
4. sampling/filtering policyを変更する前に、観測専用instrumentationまたはcapture metadataを優先する。
5. 1つの原因が支持された場合だけ、後続の測定単位でdefault-off policyを最大1つ試す。

## 対象外

- 広範なspatial denoise
- depthまたはnormal rejection thresholdの緩和
- DLSS Ray ReconstructionまたはStreamline integration
- PathTracing
- global default変更
- 前回実験への別edge-stability policyの積み重ね

## 判断gate

- varianceが高roughnessまたはtexture roughnessに対応する場合、material identityではなくsampling varianceを調べる。
- varianceがnormal-map周波数に対応する場合、radianceをfilterする前にdirection stabilityを確認する。
- historyが継続して採用されてもnoiseが残る場合、rejection変更前に収束とsample varianceを測る。
- 2領域で原因が共通しない場合、1つのfixへ押し込まず分割する。

## 2026-08-14: 同一条件の診断撮影

- 撮影専用の表示選択として `-ReflectionCaptureDebugView <name>` を追加した。既存の Hybrid Reflection automation を維持し、`pbr-params`、`normal`、`hit-material`、`evaluated-radiance`、`resolved-radiance`、`temporal-validity` を選択できる。
- frame 195 の停止後画像を1枚撮る `capture-plan-material-variance.json` を追加した。6回すべてで同じcamera timeline、camera distance scale `0.5`、stochastic sampling有効、temporal history weight `0.9`を使用した。
- PBR画像はvisible surfaceのmetallic、roughness、ambient occlusionをRGBへ格納する。normal画像はvisible GBuffer normalを示す。hit-material画像はray hit先のpayloadであり、visible surface materialとして解釈してはならない。
- evaluated radianceには面全体のsample varianceが明確に見える。resolved radianceは明らかに滑らかであり、temporal accumulationが動作し、varianceを実質的に減らしていることを確認した。
- 停止後frameのtemporal validityは全体でacceptedとなった。以前の全frameがacceptedだった証明ではないが、残留する静止noiseの唯一の原因を継続的なhistory rejectionとする見方には反する。
- 後頭部寄り領域と下面pipeは、どちらもPBR textureとnormalの変化を含む。ただし、現画像だけでは両領域を単一material channelまたは単一policyへ帰属させる根拠は不十分である。

### 現時点の判断

- temporal sequenceで反証されるまでは、残留症状をaccepted stochastic inputの遅い収束として扱う。
- rejection thresholdは緩和せず、reject画素近傍補助実験もglobalには有効化しない。
- 次の測定単位では、固定ROIについてevaluated radianceとresolved radianceのframe間varianceを別々に定量化する。

### 検証

- Debug x64 MSBuildは0 errorsで成功した。既知のvcpkg重複import warningのみ発生した。
- 6種類の自動撮影はすべて成功した。

## 2026-08-14: 固定ROIのtemporal variance

- frame 195から300まで15 frame間隔で、evaluated radianceとresolved radianceを撮影した。cameraはframe 180以降固定し、stochastic samplingとtemporal history weight `0.9`は前回の診断撮影と同じである。
- 8-frame seriesからdisplay-space luminanceのtemporal standard deviationを計算する `Measure-MaterialVariance.ps1` を追加した。これは再現可能なscreenshot症状指標であり、HDR resource自体の測定ではない。
- 枠付き画像についてuser確認を行った。最初の概略確認後、`rearward_surface`は小さくして右上へ移動し、頭頂部に隣接する後頭部寄りの指摘領域だけへ絞った。`underside_pipes`は引き続き指摘された下面pipeを覆う。
- `rearward_surface`: mean temporal standard deviationはevaluatedの`0.04266`からresolvedの`0.00964`となり、`77.40%`減少した。resolved/evaluated比は`0.2260`である。
- `underside_pipes`: mean temporal standard deviationはevaluatedの`0.03113`からresolvedの`0.00825`となり、`73.51%`減少した。resolved/evaluated比は`0.2649`である。
- 近い減少率は、temporal accumulationが両領域で有効だが、evaluatedのdisplay-space temporal deviationのおよそ4分の1を残すという共通観察を支持する。ただしmaterialまたはsamplingの根本原因が同一である証明にはならない。

### 判断

- 現在のrejection thresholdとdefault-offの近傍policyは変更しない。
- 今後のsamplingまたはfiltering実験では、確認済みの2 ROIを独立したacceptance metricとして使用する。
- policy追加前の次の診断では、history weightまたは停止後経過frameに対する収束を比較する。これによりhistory長不足と、別のestimatorまたはspatial情報を必要とするvarianceを分離する。

## 2026-08-14: History weightの収束比較

- 同じ固定cameraの8-frame seriesをresolved history weight `0.0`、`0.5`、`0.98`で撮影し、既存の`0.9` seriesとevaluated-radiance referenceを再利用した。
- `0.0`の8枚は、対応するevaluated-radiance PNGとすべてbyte単位で一致した。history weight zeroが表示結果を変えず、現在のevaluated sampleを選択することを確認した。
- `Measure-HistoryWeightConvergence.ps1`を追加した。全series deviation、frame 195-225のearly window、frame 270-300のlate window、およびearlyからlateへのmean displayed-luminance driftを出力する。
- `rearward_surface`の全series deviation: evaluated/`w0`は`0.04266`、`w50`は`0.02282`、`w90`は`0.00964`、`w98`は`0.01068`。late-window deviationは`w90`で`0.00663`、`w98`で`0.00267`となった。
- `underside_pipes`の全series deviation: evaluated/`w0`は`0.03113`、`w50`は`0.01904`、`w90`は`0.00825`、`w98`は`0.00846`。late-window deviationは`w90`で`0.00602`、`w98`で`0.00221`となった。
- `w98`はlate-window varianceが最小だが、earlyからlateへのluminance driftが大きい。`rearward_surface`で`-0.00829`、`underside_pipes`で`-0.00438`であり、`w90`の`-0.00192`、`-0.00062`より大きい。

### 判断

- default history weightを`0.98`へ上げない。late variance低下と引き換えに測定可能な遅いsettlingがあり、確認済み2領域の全series varianceも`0.9`よりわずかに悪い。
- 現在のvalidation設定は`0.9`を維持する。今回測定した範囲で最も良いbalanceという意味であり、最終production policyの宣言ではない。
- `0.9`のlate windowにもvarianceが残るため、historyをさらに延ばすだけより、後続の限定的なestimatorまたはspatial情報実験を支持する。

## 2026-08-15: Surface variance spatial実験

- temporal blend前のcurrent-sample境界へ、default-offの実験を1つだけ追加した。`Surface Variance Filter`はevaluated radianceへ3x3 filterを適用し、visible depth、normal、roughness、metallicが近いneighborだけを採用する。ほぼ完全に滑らかなvisible surfaceはfilterを通さない。
- この実験は未加重radiance contract、history weight、history rejection threshold、LightPass contribution weightingを変更しない。以前のreject画素近傍fallbackは別機能のままdefault-offを維持する。
- 撮影専用CLI flag `-ReflectionSurfaceVarianceFilter`、UI control、settings保存、GBuffer PBRParamsのread dependencyを追加した。debug noiseは既存の決定論的規則で、採用された各neighborへ適用する。
- Debug x64 MSBuildは0 errorsで成功し、既知のvcpkg重複import warningのみ発生した。filter有効の8-frame seriesもすべて撮影できた。
- filter有効のD3D12 Debug Layer captureではerrorはなく、既存のcommitted buffer initial-state warningが2件だけ記録された。
- history weight `0.9`でtemporal-only baselineと比較すると、mean temporal deviationは`rearward_surface`で`51.20%`、`underside_pipes`で`53.49%`追加減少した。mean displayed luminanceの変化はそれぞれ約`+0.00153`、`+0.00108`である。
- 停止後前半・中盤・後半を比較する日英HTML suiteを追加した。user report `hybrid-reflection-material-variance-filter-v1-report-2026-08-14T21-39-51.610Z-a66a127d.json`は9項目すべて合格し、選択defectとnoteはなかった。

### 判断

- 実験実装は残し、default-offを維持する。静止領域の客観・主観gateには合格したが、このphaseではmotion/disocclusion挙動をdefault有効化に十分な強さで確認していない。
- このphaseで別estimatorを積み重ねたりfilterを広げたりしない。次のgateは、この1実験だけを使った同一条件のmotion/reversal A/B確認とする。
