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

### 2026-08-23: ReflectionRayHit coverage validation

ユーザーvisual validationでReflection Debug `Hit`の全項目がPASSした。

- 明るいgeometry-hit領域が存在する;
- 暗いenvironment-miss領域が存在する;
- sphere間またはsphere内部でhit/miss差を識別できる。

したがって、制御sceneはgeometry-hit sampleとenvironment-miss sampleの両方を提供する。Evaluated Radiance behaviorと固定ROI選定は未完了である。

### 2026-08-23: Evaluated Radiance deterministic/stochastic比較

`Stochastic Rough Sampling`無効時、ユーザーはroughnessが横方向に変化し、IBLの見え方が変わることを確認した。sphere、floor、emissive targetのgeometry reflectionは同程度にくっきりしていた。この未加重debug signalでは、metallic/dielectric rowの差を位置差から明確に分離できなかった。これは現在のcontractと整合する。visible-surface Fresnelとfinal contribution weightingは`LightPass`が所有し、deterministic geometry rayはmirror directionを使用するためである。

`Stochastic Rough Sampling`有効時、1920x1080のEvaluated Radiance captureでroughness条件に沿ったdisplay-space grainの強い横方向変化を確認した。複数のsphereとfloorには高密度のstochastic sampleが現れ、rowの反対側は大幅に安定し、くっきりした状態を維持していた。このcaptureによりstochastic direction pathが動作し、見えるvarianceがroughness条件へ強く依存することを確認した。このcaptureだけからroughness値と画面左右の対応は推測しない。

続いてユーザーがscreen-space orderingとtemporal behaviorを確認した。画面右端のsphereがroughness `0.0`であり、安定している。画面上で逆順に並ぶroughness条件のうち、左から3列目前後、scene値では概ねroughness `0.35`付近からtemporal noiseが気になり始め、よりroughな側でも確認できる。これは測定済みvariance境界ではなく主観的なthresholdである。

残りのscope内観察は次のとおり記録した。

- geometry reflectionの位置または形状がframe間で変化する: PASS;
- Evaluated Radianceではmetallic/dielectric差が小さいままである: PASS;
- NaN相当、全面白、固定黒の破綻は明確には見られない: ユーザーの「多分YES」に基づく暫定PASSであり、網羅的な数値監査ではない。

収束とpersistent temporal artifactがないことはまだ確認していない。固定ROIには、安定したroughness `0.0` controlと、観察されたthreshold以上のnoisy conditionを少なくとも1つ含める。

### 2026-08-23: Diagnostic scene selection contract

- 既存linear-HDR diagnostic runnerから制御sceneを明示選択できるよう、`-AutoSelectHybridReflectionEstimatorTest`を追加した。
- 明示的なscene selection flagがない場合、`-ReflectionHdrDiagnostics`は従来どおりDamagedHelmetをdefaultとし、Phase 1 workflowを維持する。
- DamagedHelmetとEstimator Testのauto-selection flagは相互排他とする。
- reportがscene前提を暗黙に混在させないよう、HDR diagnostic JSONへloaded scene名を記録する。
- 固定ROI座標はvisual overlay確認待ちとし、automation contractへ推測座標を埋め込まない。
- Debug x64/HLSL buildは成功し、既存のvcpkg重複import warningだけが報告された。

### 2026-08-23: 固定ROI検証と64-frame smoke

最初のoverlayは手動captureを使用しており、CLI automationとはcamera framingが異なっていた。最初のdiagnostic runで2 ROIが全ゼロsignalとなったため、この不一致を検出できた。これらの座標とreportをroughness結果として解釈せず、不採用とした。

HDR diagnosticsと同じauto-selected sceneおよびhidden-UI経路で、新しい1920x1080 stochastic Evaluated Radiance captureを生成した。上段metallic rowのsphere surface内でsilhouetteを避け、3つの48x48 rectangleを配置した。ユーザーは3位置とroughness差をすべて確認した。

| ID | Rectangle | 条件 |
| --- | --- | --- |
| `roughness_1_metal` | x `484`, y `396`, width `48`, height `48` | high-variance roughness `1.0` |
| `roughness_035_metal` | x `844`, y `396`, width `48`, height `48` | 主観noise thresholdのroughness `0.35` |
| `roughness_0_metal_control` | x `1392`, y `396`, width `48`, height `48` | 安定したmirror-limit control |

各ROIを独立processで開始し、stochastic sampling有効、history weight `0.9`、32-frame warm-up後に64 framesを測定した。

| ROI | Evaluated mean | Evaluated variance | Evaluated CV | Frame-difference p99 | Evaluated maximum | Resolved variance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness `1.0` | `0.245015` | `1.569857` | `5.113725` | `11.969035` | `12.061967` | `0.0630104` |
| roughness `0.35` | `0.138602` | `0.0365103` | `1.378601` | `0.286579` | `12.077592` | `0.00142675` |
| roughness `0.0` | `0.189160` | ほぼ`0` | ほぼ`0` | `0` | `0.664890` | ほぼ`0` |

結果は主観的な順序を再現した。roughness `0.0`はdeterministicで安定し、roughness `0.35`には測定可能なtemporal varianceがあり、roughness `1.0`ではvarianceとframe差分が大幅に増加する。これはdiagnostic smoke結果であり、estimator correctnessまたは収束を主張しない。全runで期待したscene名、64 frames、error 0件、既存のcommitted-buffer initial-state warning type 3回を確認した。hit/accept/depth-reject/normal-reject率は全reportに含まれる。

## 対象外

- production temporal/spatial denoiser;
- DLSS RR backend integration;
- Path Tracing pass;
- 大規模RenderGraph refactor;
- default-off Surface Variance Filterの昇格。
