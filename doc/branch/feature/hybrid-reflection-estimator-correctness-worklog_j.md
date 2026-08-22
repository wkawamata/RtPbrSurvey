# features/hybrid-reflection-estimator-correctness

## 目的

PDFまたはBRDF throughput補正を追加する前に、stochastic Hybrid Reflectionが何を推定するestimatorなのかを監査して固定する。Phase 1のlinear-HDR診断を再利用し、variance、signal preservation、estimator correctnessを分離する。

既存のHigh-SPP Current-Estimator Mean Baselineをphysical ground truthとして扱わない。

## 初期監査

### Sampling

- `ReflectionSampling.hlsli`はpixel/frameごとにdeterministicな2D sampleを生成する。
- `alpha = roughness * roughness`としてGGX NDF half-vectorをsampleし、visible-surface view directionをhalf-vectorでreflectする。
- GGX VNDF samplingではなくNDF samplingである。
- below-surface directionはdeterministic mirror directionへfallbackする。このfallbackはsampling distributionを変更するが、現在は確率accountingがない。
- roughnessが`0.001`以下の場合はmirror directionを使用する。

### Signal evaluation

- `HybridReflectionPass`はsampled directionをtraceし、raw hit/material payloadを保存する。
- `ReflectionEvaluatePass`はpixel/frame入力から同じsampled directionを再構築する。
- stochastic sampling有効時のmissはsharp environment mipをsampleする。
- hit時はvisible surface方向へ出るhit surface radianceを評価し、direct light、diffuse/specular IBL、emissionを含める。
- evaluated resultはvisible surfaceに関して未加重のままである。distance、visible roughness、intensity、Fresnel contributionは引き続き`LightPass`が所有する。

### 不足しているMonte Carlo estimator項

- 明示的なhalf-vector PDFなし;
- half-vector PDFからreflection-direction PDFへの変換なし;
- sampled-radiance signal内にvisible-surface Cook-Torrance BRDF factorなし;
- `f_r * L_i * (N dot L) / p(L)` throughputなし;
- mirror fallback確率のaccountingなし;
- estimator confidenceまたはsample PDF payloadなし。

## 実装前に必要なcontract判断

現在の経路は、incident one-bounce radianceのstochastic rough-direction近似を生成し、`LightPass`でvisible-surface heuristic weightingを適用するものと説明するのが適切である。visible-surface BRDF積分のMonte Carlo estimatorではない。

`ReflectionEvaluatePass`内へPDF補正だけを追加するのは安全ではない。visible-surface Fresnelとcontribution weightingを`LightPass`へ意図的に遅延しているためである。正しいMonte Carlo estimatorへ進む場合は、このownershipの一部を移動または再定義する必要がある。Phase 2では最初に次のどちらかを選び、文書化する。

1. 現在の未加重radiance contractを維持し、stochastic samplingをbounded approximationとして扱う。
2. directional PDFとthroughputを持つ明示的なBRDF-integral estimator contractを導入し、二重weightingを避けるようfinal contribution ownershipを見直す。

この判断とPhase 1診断の再実行が完了するまでproduction defaultを変更しない。

## 計画gate

1. estimator targetとownership判断を文書化する。
2. invalid/fallback behaviorを含め、現在のGGX NDF half-vector PDFとdirectional PDFを導出する。
3. 選択したestimatorにおけるmirror limit、environment miss、geometry hit semanticsを固定する。
4. PDFとthroughputを確認するために必要な最小payload/debug dataだけを追加する。
5. roughness条件ごとにdeterministic IBLとHigh-SPP Current-Estimator Mean Baselineを比較する。どちらもphysical ground truthとは呼ばない。
6. estimator変更後にPhase 1 paired HDR診断を再実行する。

## 制御評価scene

### 2026-08-22: 初期scene実装

- 外部assetへ依存しない`Hybrid Reflection Estimator Test`を追加した。
- 同一sphere 12個を固定2-row gridへ配置した。
- columnは左からvisible roughness `0.0`、`0.05`、`0.15`、`0.35`、`0.6`、`1.0`とする。
- 上段はmetallic `1.0`、下段はdielectric metallic `0.0`とする。
- sphere materialはすべて同じneutral albedo、normal mapなし、emissionなし、ambient occlusion 1.0とする。
- roughなdark floorで安定したgeometry/depth contextを作る。
- sphere gridを遮らずgeometry hitの高radiance候補を作るため、off-axisに細いemissive targetを置く。
- camera position、gaze、FOV、near plane、far planeをscene codeで固定する。animationはない。
- Debug x64/HLSL buildは成功し、既存のvcpkg重複import warningだけが報告された。

このsceneはvisual showcaseではなく測定器である。固定1920x1080 ROIと、emissive targetが意図したhit/miss coverageを生成することの証明はvisual validation待ちとする。codeだけから座標を推測しない。

### 2026-08-23: Base scene visual validation

ユーザーvisual validationでbase sceneの5項目すべてがPASSした。

- sphere 12個がすべて表示される;
- roughnessの段階変化を識別できる;
- metallic/dielectricの上下差を識別できる;
- emissive targetがsphere gridを遮らない;
- floorとcamera framingに問題がない。

これによりbase composition/framing gateを完了する。ReflectionRayHit/Evaluated Radianceのhit/miss coverage確認と、その後の固定1920x1080 ROI選定は未完了である。

## 対象外

- production temporal/spatial denoiser;
- DLSS RR backend integration;
- Path Tracing pass;
- 大規模RenderGraph refactor;
- default-off Surface Variance Filterの昇格。
