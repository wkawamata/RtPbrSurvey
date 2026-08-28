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
