# Hybrid Reflection Dynamic Temporal Diagnostics 作業ログ

## 2026-08-28: Branch baseline

Branch: `features/hybrid-reflection-dynamic-temporal-diagnostics`

Base: `a3f052c` (`Add RenderGraph diagnostic node viewer (#35)`)

このbranchは、追加filter policyを設計する前にdynamic temporal behaviorを診断する。以前報告された主観的な追従遅れを確認済み不具合とは仮定しない。controlled `Hybrid Reflection Estimator Test` sceneをprimary diagnostic sceneとし、DamagedHelmetはsecondary generalization checkとして残す。

### 統合済みbaseline

- Hybrid reflection resource／pass contractは`main`へ統合済みである。
- stochastic rough samplingはexperimental default-offを維持する。
- temporal reflection historyはmotion reprojectionとdepth／normal rejectionを使用する。
- persistent confidenceとvariance-guided temporal weightingはexperimental default-offを維持する。
- 直前のlive Lit gateではmotion、方向反転、停止後のregressionは観察されなかった。静止noise輝度がわずかに低下した可能性はあるが、visible A／B差は小さかった。
- temporal pass／resource ownership確認にRenderGraph diagnosticsを利用できる。reflection history ping-pong nodeは交互frame間でも表示位置が安定する。

### 診断する問い

1. camera／object motion中にmotion vectorが期待するprevious-frame history pixelを選ぶか。
2. bounds、motion／reprojection、depth、normal、explicit resetのどの条件がhistoryをrejectするか。
3. reflection hit distanceまたはhit identityが変化したとき、accepted historyは妥当か。
4. motion停止後、resolved radianceは何frameでsettleするか。
5. Litで見えるartifactはtemporal contractまたはpolicy変更を正当化するほど重要か。

### 予定measurement

- 小さな固定ROI／probe setについてcurrent／previous pixel coordinateをframeごとに記録する。
- 単一validity bitだけでなくhistory acceptanceと個別rejection reasonを記録する。
- current／previous depth、normal agreement、motion vector、roughness、hit flag、hit distanceを記録する。
- evaluated／resolved linear-HDR luminanceを記録し、deterministic motion停止後のT50／T90／T95 settling framesを計算する。
- camera motion、object motion、方向反転、disocclusion、stationary controlを分離する。
- `.0`／`.1` physical resourceを統合せず、RenderGraph diagnosticsでping-pong ownershipを確認する。

### Gateと主張範囲

- PASS: rejection／acceptance sequenceを記録inputから説明できる。
- PASS: 固定timeline／reset pointでsettling metricを再現できる。
- PASS WITH LIMITATION: debug-view response差をLitの知覚的重要性と分離して記録する。
- NOT CLAIMED: production denoiser readiness、確認済みobject-motion bug、physical ground truth、default history weight変更の必要性。

次は既存motion-vector、temporal-validation、HDR diagnostic pathを監査し、最小report schema拡張を特定する。

## 2026-08-28: Diagnostic schema v11 baseline

監査により、既存resolved-radiance alpha channelはno history、outside-history reprojection、depth rejection、normal rejection、accepted historyをすでに区別していることを確認した。従来のHDR reportが公開していたのはacceptance、depth rejection、normal rejection rateのみだった。`ReflectionRayHit.r`にはhit distanceがすでに格納されているため、新規ray payloadは不要である。

このため、最小report拡張を次に限定した。

- 既存temporal status metadataから`noHistoryRate`と`outsideHistoryRate`を分離して出力する。
- 既存ray-hit payloadからhit pixel限定の`meanHitDistance`を出力する。
- 既存`GBuffer.MotionVector`のROIをreadbackし、stored NDC vector magnitudeのmean／maximumを出力する。
- report上のmotion値はtemporal jitter／configured offset除去前のstored valueであることを明記する。
- rendering／temporal policyのdefaultを変更せず、HDR diagnostic report schemaを10から11へ更新する。

Validation:

- Debug x64／HLSL build: PASS。error 0件。既存のvcpkg duplicate import warningは継続する。
- controlled sceneの8-frame GPU smoke test: PASS。schema 11と全追加fieldが出力された。
- stationary control: warm-up後のmotion magnitudeは0、temporal acceptance rateは1.0、全rejection rateは0で、期待どおりだった。
- D3D12 Debug Layer: error 0件。bufferのinitial UAV stateを無視する既存warningが3件あり、新規diagnostic copy warningは観察されなかった。

このcheckpointではdynamic motion／settlingはまだ測定していない。次はdeterministic camera-motion timelineを追加し、既存のper-frame linear-HDR meanからsettling metricを計算する。

## 2026-08-28: Deterministic camera timeline／settling contract

HDR diagnosticsで`-ReflectionOrbitDegrees`と`-ReflectionOrbitFrames`を再利用し、warm-up後に次のdeterministic measurement timelineを定義した。

1. 設定frame数だけ正方向へorbitする。
2. 同じframe数で反転し、初期yawへ戻る。
3. 残りのmeasurement中は静止する。

automation pathはarcball state変更後にcameraを明示更新する。hidden／non-foreground automation runではこの処理が必要であり、既存screenshot／keyframe automation pathも同時に修正する。各diagnostic frameへautomation-frame index、phase、yaw offsetを記録する。report schema 12へtimeline／settling contractを追加した。

settlingはresolved-radiance ROI mean luminanceから測定する。終端8 stationary sampleの平均をsettled valueとする。T50／T90／T95は、停止直後のinitial errorに対する残差がそれぞれ50%、10%、5%以下の状態を3 sample連続で満たす最初のoffsetである。initial errorがsettled meanの1%または`1e-6`以下の場合は、正規化できる意味のある応答振幅がないためmetricをinvalidとする。

GPU validationはcontrolled estimator scene、10度forward／reverse orbit、history weight 0.9、stochastic sampling、32x32 ROIで実施した。

- 短いsmoke runの移動frameでは、約0.0024～0.0028 NDCのnonzero mean motionを記録した。
- reverse frameでは約0.9～1.4%のdepth rejectionが発生し、このROIではmaterial normal rejectionは発生しなかった。
- stationary frameではmotion 0、history acceptance 1.0へ戻った。
- 32-sample settling runのinitial stop errorは`3.4e-5`で、minimum meaningful threshold `7.9e-4`を下回った。このため架空のlatency値を出さず、settlingは正しくinvalidと判定された。
- Debug x64／HLSL buildはerror 0件で成功した。D3D12 Debug Layerに新規error／warningはなかった。

次はより強いmotion条件と空間的に異なるcontrolled-scene ROIを測定し、measurable dynamic responseの有無を確認してからLit perceptual gateへ進む。

## 2026-08-29: 30度controlled-scene dynamic measurement

visual validation済みのmetallic row 48x48 ROI 3か所を、より強い30度orbit、forward 12 frames、reverse 12 frames、history weight 0.9、stochastic sampling、32 warm-up framesで測定した。roughness 1.0／0.0は48 measurement samplesを使用した。roughness 0.35は、予備48-sample windowでT95を3 sample連続確認できなかったため64 samplesへ延長した。延長runは先頭48 resolved-mean samplesを完全に再現した。

| ROI | Moving motion mean (NDC) | Moving acceptance mean | Moving depth reject mean | Moving normal reject mean | Stationary acceptance | T50 | T90 | T95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness 1.0 metal | 0.003949 | 0.9572 | 0.0424 | 0.0004 | 1.0000 | 6 | 11 | 13 |
| roughness 0.35 metal | 0.013917 | 0.9407 | 0.0590 | 0.0003 | 1.0000 | 6 | 19 | 20 |
| roughness 0.0 metal | 0.014832 | 0.9494 | 0.0439 | 0.0067 | 1.0000 | 6 | 15 | 17 |

motion magnitude差は主としてscreen position／projected motion差であり、roughnessだけへ帰属してはならない。これらのROIではoutside-history rejectionは発生しなかった。全3 caseで停止後にmotion 0、history acceptance 1.0へ戻った。settling responseは測定可能で、T90は11～19 frames、T95は13～20 framesだった。

weight 0.9の純粋なfixed EMAは、旧historyが10%残るまで約22 frames、5%まで約29 framesを要する。今回のROIがそれより速くsettleしたのは、測定signalが途切れないscalar EMAだけではなく、reprojection／rejectionと空間的に変化する内容を含むためである。このcontrolled 30度camera orbitでは、過度に長いhistory tailを示す数値結果は得られなかった。これはcamera-motion resultでありobject-motion conclusionではない。また、これだけでLit outputの知覚的許容性を確立しない。

全4 processは正常終了した。D3D12 Debug Layerはerror 0件で、各processに既知のbuffer initial-state warning 3件だけがあった。

次は同じforward／reverse／stop behaviorをlive Lit perceptual gateで確認する。Litで実用的artifactが見つかった場合にのみ、object-motion／hit-identity diagnosticsを予定より前へ昇格する。
