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

## GGX NDF PDF導出

`N`をvisible-surface normal、`V`をsurfaceからcameraへ向かう方向、`L`をsampleされたoutgoing reflection direction、`H = normalize(V + L)`をhalf-vectorとする。shaderは`viewDirection = -V`を受け取り、`H`をsampleした後、`L = reflect(-V, H)`を評価する。

codeは`alpha = roughness^2`とする。逆変換は次のisotropic GGX normal distributionをsampleする。

`D(H) = alpha^2 / (pi * ((N dot H)^2 * (alpha^2 - 1) + 1)^2)`

NDFをprojected-area measureでsampleするため、half-vector densityは次になる。

`p_H(H) = D(H) * max(N dot H, 0)`

有効なreflection directionでは、half-vectorからdirectionへのJacobianにより次になる。

`p_L(L) = p_H(H) / (4 * abs(V dot H))`

これは現在の有効GGX NDF sampleに対応するdirectional PDFである。VNDF PDFではなく、visibility term `G1(V)`を含まない。

### Invalid branchとmirror branch

- `roughness <= 0.001`は独立したdeterministic mirror branchである。delta distributionであり、通常の有限directional PDFとして表現しない。
- `roughness > 0.001`で`N dot L <= 0`となるsampleは、現在mirror directionへ置換される。
- したがって実装上の分布は、上記のcontinuous valid-direction densityと、invalid sampleの全確率質量をmirror-direction deltaへ集約した混合分布である。
- 集約される確率質量は`N`、`V`、roughnessに依存するが、shaderでは計算していない。
- このfallbackを維持したまま`f_r * L_i * (N dot L) / p_L(L)`を適用しても、正しいestimatorにはならない。mirror deltaとcontinuous branchには別々のprobability accountingが必要である。

### 推奨estimator contract

明示的なBRDF-integral pathでは、監査可能な最小contractを次とする。

1. roughness `0`/mirror-limit評価を名前付きdeterministic branchとして維持する。
2. stochastic branchでは`N dot L <= 0`をinvalid sampleとしてzero contributionで返す。mirrorへremapせず、rayもtraceしない。
3. `p_L(L)`を保持または再構築し、visible-surface Cook-Torrance termを`f_r * L_i * max(N dot L, 0) / p_L(L)`として適用する。
4. environment missとgeometry hitを同じincident-radiance sample `L_i`の異なるsourceとして扱い、sampling PDFとvisible-surface throughput semanticsを共有する。
5. この明示的estimator pathではvisible-surface Fresnel/BRDF ownershipを後段`LightPass`のheuristic weightingから移動するか、重複factorを除く。paired HDR diagnosticsを通過するまで現在pathをcomparison modeとして維持する。

below-surface directionをzeroで返すことで元のsampling distributionを維持し、zero-integrand domainを明示できる。有効directionだけをresampleするとconditional distributionになり、そのnormalizationが必要になる。GGX VNDFは後続の比較候補として残し、最初のcorrectness実装の前提にはしない。

## Estimator signal ownership判断

Phase 2では既存approximation pathを維持し、`ReflectionSpecularEstimate`という独立したexperimental current-frame signalを追加する設計を選択した。

- `ReflectionEvaluatedRadiance`は未加重incident one-bounce radianceのままとする。
- `ReflectionResolvedRadiance`は同じ未加重semanticsを持つtemporal outputのままとする。
- `ReflectionSpecularEstimate`は、対応するcurrent-frame `L_i` sampleへvisible-surface Cook-Torrance BRDF、cosine、directional-PDF補正を適用する。
- user intensity、distance fade、final scene composition、exposure、tone mappingを含まない。
- 最初のsliceはdebug/diagnostic-onlyとし、既存temporal historyまたは`LightPass`へ接続しない。
- 将来の`ReflectionResolvedSpecularEstimate`はfinite-value、mean、variance、firefly、mirror-limit evidenceをgateとする。
- resolved estimateを将来`LightPass`へ接続する場合、visible-surface Fresnelおよびroughness/BRDF weightingを再適用しない。

これによりresource semanticsをmodeによって動的に変えず、未加重`L_i`を蓄積した後に無関係なcurrent-frame throughputを乗算する誤りも避ける。focused contract文書にも同じ境界を反映した。

### 2026-08-23: Sampling result helper

- direction、directional PDF、validity、deterministic-mirror分類を持つ`RoughReflectionSample`を追加した。
- helperは現在のGGX NDF half-vector densityを計算し、`1 / (4 * abs(V dot H))` Jacobianでreflection-direction densityへ変換する。
- roughness `0.001`以下はPDF 0の名前付きdeterministic mirror branchとして返す。有限PDFのstochastic sampleとして解釈しない。
- below-surfaceまたはnon-finite/zero-PDF stochastic sampleは、診断用に元のsampled directionを保持しつつ`valid = 0`を返す。
- 既存`SampleRoughReflectionDirection` wrapperは現在のapproximation path向けmirror fallbackを維持するため、このsliceでは意図的に描画結果を変更しない。
- 将来の明示的estimator pathはvalidityを直接使用し、invalid rayをtraceせずzeroを返す。
- Debug x64 Rebuildで全HLSLを強制再compileし、成功した。build warningは既存のvcpkg重複import warningだけである。
- 同一条件のEvaluated Radiance captureをhelper導入前captureと比較した。PNG hashは異なったが、RGBA直接比較では1920x1080中の差は2 pixelsだけで、channel差の最大値は1、p99は0だった。bit-exact identityではないが、描画出力は同等と判定する。
- runtime captureはD3D12 error 0件で、既存のcommitted-buffer initial-state warning type 3回だけを報告した。

### 2026-08-23: Estimator math helper

- pass outputまたはresource bindingを変更せず、副作用のない`EvaluateRoughReflectionSpecularEstimate` helperを追加した。
- stochastic branchはCook-Torrance `D * G * F / (4 * NdotV * NdotL)`を評価し、`L_i * NdotL`を乗算して対応するdirectional PDFで除算する。
- `D`はdirection samplingと同じ`alpha = roughness^2` GGX parameterizationを使用する。
- `G`には既存direct-light用Schlick近似ではなく明示的なisotropic GGX Smith `G1(V) * G1(L)`を使い、sampling modelに対してestimator mathを監査可能にする。
- deterministic mirror branchは有限PDFを仮定せず、incident radianceへSchlick Fresnelを乗算して返す。
- invalid stochastic sampleはzeroを返す。
- このsliceでは意図的に`ReflectionSpecularEstimate` storageを追加しない。resource/MRT wiringを次の境界とする。
- Debug x64 Rebuildで全HLSLを強制再compileし、error 0件、既存のvcpkg重複import warningのみで成功した。

### 2026-08-23: ReflectionSpecularEstimate MRT storage

- `ReflectionEvaluatePass`を1 render targetから2つの`R16G16B16A16_FLOAT` MRT targetへ拡張した。
- target 0は既存の未加重contractを持つ`ReflectionEvaluatedRadiance`のままとする。
- target 1を独立current-frame `ReflectionSpecularEstimate`とし、visible albedo、metallic、roughness、normal、view direction、対応するdirection sample、evaluated incident radianceをestimator math helperへ渡す。
- render-size resource、persistent SRV descriptor、RTV descriptor、RenderGraph resource name、write dependency、binding resolver、resize/release処理、PSO target formatを追加した。
- resourceはtemporal historyまたは`LightPass`から消費せず、debug UIまたはHDR readbackにもまだ公開しない。
- 全HLSLを含むDebug x64 Rebuildは既存のvcpkg重複import warningのみで成功した。
- runtime自動captureは正常終了し、D3D12 error 0件、既存のcommitted-buffer warning type 3回だけを報告した。
- 既存Evaluated RadianceはMRT導入前の同一条件captureとpixel単位で完全一致し、1920x1080で差分pixelは0だった。

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

### 2026-08-23: Specular estimate debug公開

- 既存Evaluated Radiance経路を変更せず、`ReflectionSpecularEstimate`をbindするReflection Debugの`Specular Estimate` viewを追加した。
- deterministicな自動screenshot用に`-ReflectionCaptureDebugView specular-estimate`を追加した。
- このviewが適用するのは既存の表示用tone compressionだけであり、Temporal ReflectionまたはLightPassへ入力しない。
- 制御estimator sceneの64-frame smoke captureは正常完了し、期待するroughnessおよびmetallic応答を確認した。
- 全HLSL targetを含むDebug x64 full rebuildは成功した。runtime captureはexit code 0、D3D12 error 0件で、既存committed-buffer warningの3回反復だけが残った。
- このsignalのlinear-HDR readbackと数値統計を次のdiagnostic stepとする。

### 2026-08-23: Specular estimate linear-HDR診断

- HDR diagnostic captureへ`ReflectionSpecularEstimate` readbackと独立したtemporal統計を追加した。
- JSON report schemaをversion 2へ進め、frameごとの`specularEstimateMeanLuminance`と集計`statistics.specularEstimate`を追加した。
- 既存Current-Estimator Mean Baselineは未加重Evaluated Radianceだけから定義する。Specular EstimateとのcontractをまたぐRMSEは報告しない。
- 検証済みの上段metallic 48x48 ROI 3か所で、32-frame warm-upと64-frame measurementを再実行した。

| ROI | Mean | Variance | CV | Frame-difference p99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| roughness `1.0` | `0.0432188` | `0.0805466` | `6.56676` | `2.73469` | `5.26145` |
| roughness `0.35` | `0.0627360` | `0.00662843` | `1.29774` | `0.138190` | `6.44923` |
| roughness `0.0` | `0.0875360` | ほぼ`0` | ほぼ`0` | `0` | `0.307697` |

実験estimator signalは期待したvariance順序を再現し、mirror-limit controlはdeterministicなままである。この64-frame結果が確認するのはobservabilityとfinite-value smoke gateまでであり、unbiasedness、物理的正しさ、収束は確立しない。Debug x64/HLSL full rebuildは成功した。3 runすべて正常終了し、D3D12 errorは0件、既存committed-buffer warningの同じ3回反復だけを確認した。

### 2026-08-23: 256-frame estimator signal監査

同じ3 processをresetし、同一の32-frame warm-up、ROI、scene、camera、stochastic設定で256-frame measurementを再実行した。sample sequenceをresetしたため、先頭64 measurementsは以前の64-frame reportを再現し、paired-prefix checkとして機能する。

| ROI | 64-frame mean | 256-frame mean | Mean変化 | 先頭64から末尾64の変化 | 256-frame variance | 256-frame p99 difference | 256-frame maximum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness `1.0` | `0.0432188` | `0.0443692` | `+2.66%` | `+3.79%` | `0.0855786` | `2.82994` | `5.26145` |
| roughness `0.35` | `0.0627360` | `0.0624920` | `-0.39%` | `-0.62%` | `0.00648346` | `0.136274` | `7.82340` |
| roughness `0.0` | `0.0875360` | `0.0875360` | `0%` | `0%` | ほぼ`0` | `0` | `0.307697` |

roughness `0.35`のmeanはこのwindowで1%以内に安定し、mirror-limit controlは完全に安定したままである。roughness `1.0`は64-frame block間で無視できないmean移動が残り、この条件の長期meanが安定したとは256 framesでは主張できない。またroughness `0.35`のmaximum増加は、meanが安定していても長いrunほどrare high-value sampleを観測することを示す。roadmapのconditional gateに従い、roughness `1.0`と`0.35`には1024-frame追加監査を行う根拠が生じた。deterministicなroughness `0.0` controlを1024 framesで繰り返す実益はない。全runは正常終了し、D3D12 errorは0件、既存warningの同じ3回反復だけを確認した。

### 2026-08-24: 条件付き1024-frame監査

追加監査はgateを発火させた2つのstochastic条件へ限定した。roughness `0.0`はanalytic mirror値をさらに768 frames繰り返さず、256-frame deterministic-control結果を採用した。

| ROI | 256-frame mean | 1024-frame mean | Mean変化 | 連続する4つの256-frame mean | Block range / 1024 mean | Maximum 256 -> 1024 |
| --- | ---: | ---: | ---: | --- | ---: | ---: |
| roughness `1.0` | `0.0443692` | `0.0443573` | `-0.0268%` | `0.0443692`, `0.0439510`, `0.0447227`, `0.0443862` | `1.74%` | `5.26145` -> `5.29550` |
| roughness `0.35` | `0.0624920` | `0.0624922` | `+0.0002%` | `0.0624920`, `0.0626205`, `0.0624548`, `0.0624013` | `0.35%` | `7.82340` -> `7.82340` |

判定: このdeterministic sequenceと2つの固定ROIにおけるcurrent estimator signalの経験的な長時間mean安定性は、**PASS WITH LIMITATION**とする。roughness `1.0`の256-frame blockは1024-frame meanに対して最大`1.74%`動き、temporal CVも`6.60`のままであるため、強い1 spp varianceは解消していない。unbiasedness、physical referenceとの一致、scene generalization、production readinessは主張しない。2 runとも正常終了し、D3D12 errorは0件、既存warningの同じ3回反復だけを確認した。

### 2026-08-24: Estimator数式監査

shaderの方向規約を含め、実装済みstochastic branchをend-to-endで確認した。

| 項目 | 実装関係 | 監査 |
| --- | --- | --- |
| sampled direction | `L = reflect(-V, H)` | 整合 |
| GGX NDF parameter | `alpha = roughness^2` | sampling、PDF、BRDFで整合 |
| half-vector density | `p_H(H) = D(H) * max(N dot H, 0)` | 整合 |
| directional density | `p_L(L) = p_H(H) / (4 * abs(V dot H))` | 整合 |
| BRDF | `D * G1(V) * G1(L) * F / (4 * NdotV * NdotL)` | 整合 |
| estimator throughput | `L_i * f_r * NdotL / p_L(L)` | 整合 |
| below-surface stochastic sample | zero contribution | 積分領域と整合 |
| mirror limit | analytic Fresnel branch、finite PDFなし | delta境界として整合 |
| hitとmiss | 同じsampled `L_i`の2 source | estimator ownershipとして整合 |

この監査では数式上の不一致を検出しなかった。ただし、これはphysical-reference testではない。制御rendered sceneのincident radianceは、geometry hit、environment miss、secondary-surface lightingを通じてsampled directionごとに変化する。この結合があるため、現在のROI reportだけではBRDF/PDF biasを分離できない。

次の限定的なtest unitはdefault-off constant-incident-radiance diagnostic modeとする。sampled directionとestimator throughputを維持しつつ、`ReflectionSpecularEstimate`だけのhit/miss依存`L_i`を既知の定数へ置き換える。approximation resource、Temporal Reflection、LightPassは変更しない。その長期meanを同じCook-Torrance modelの独立した数値hemisphere積分と比較する。既存の未加重baselineやdeterministic prefiltered IBLをphysical ground truthとは呼ばない。

## 対象外

- production temporal/spatial denoiser;
- DLSS RR backend integration;
- Path Tracing pass;
- 大規模RenderGraph refactor;
- default-off Surface Variance Filterの昇格。
