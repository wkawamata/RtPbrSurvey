# features/hybrid-reflection-variance-guided-processing

## 目的

Phase 2で確立したweighted Hybrid Reflection estimator signalを対象に、variance-guided処理を設計・検証する。signal ownership、variance observability、限定されたpolicy実験から開始する。既存3x3 Surface Variance Filterを昇格させず、variance低減だけからproduction denoiser readinessを主張しない。

## 開始地点

- Phase 2はPR #33により`105f6da`として`main`へ統合済みである。
- `ReflectionSpecularEstimate`は、相関したincident-radiance sample、Cook-Torrance BRDF、cosine、directional-PDF throughputを含むcurrent-frame linear-HDR signalである。
- 現行Temporal Reflectionは未加重の`ReflectionEvaluatedRadiance`を蓄積し、LightPassが後段でvisible-surface Fresnelとroughness policyを適用する。独立したweighted-history transitionが検証されるまで、このpathは維持する。
- default-off Surface Variance Filterは、未加重Evaluated Radianceに対する固定3x3 pre-temporal filterである。測定variance estimateを持たないため、variance-guidedと呼び替えない。
- scoped Lit orbitでは軽微なtemporal noiseが見られたが、userは許容範囲の可能性があると評価した。したがってdynamic temporal診断は最初の作業ではなく条件付きのままとする。

## 初期contract方針

variance-guided pathは最終的にweighted estimator signalを処理し、未加重radiance historyとcurrent-frame BRDF/PDF throughputを混在させない。想定するfuture signalは`ReflectionResolvedSpecularEstimate`であり、`ReflectionSpecularEstimate`をtemporal resolveしたlinear-HDR weighted-specular semanticsを維持する。

variance metadataはradiance alphaと分離する。最初の候補contractはluminance first/second momentsを持つ独立temporal-moments historyであり、そのhistory validityとrejection/resetはresolved weighted estimateと同じownership/reset lifecycleに従う。history lengthまたはconfidence signalは、測定されたadaptive policyが必要とする場合だけ追加する。

この初期noteではresource追加を確定しない。format、precision、momentをtemporal clippingの前後どちらで計算するかはaudit判断として残す。

## 最初の作業単位

1. `ReflectionSpecularEstimate`からfuture resolved weighted estimate、さらにLightPassへ移行するproducer/consumerを監査し、Fresnelまたはroughness weightingの二重適用を防ぐ。
2. temporal moment update、rejection/reset、diagnostic debugの意味を定義する。
3. adaptive filteringより先にobservabilityを追加する。mirror controlはほぼzero varianceを示し、roughness `0.35`/`1.0`は測定済みvariance orderingを再現すること。
4. gate通過後にだけ、1つの限定adaptive policyを固定temporal accumulationとpaired 64/256-frameで比較する。

## Acceptance境界

### 主張可能

- 名前付きsignal、ROI、frame window、deterministic sequenceに限定したvariance/confidence挙動;
- 限定されたdefault-off実験のmean preservationとvariance変化;
- 評価対象Lit条件に対するadaptive policyの改善有無。

### まだ主張しない

- production denoiser readinessまたはscene generalization;
- current-estimator mean baselineからのphysical correctness;
- universal temporal stability、unbiasedness、Path Tracing同等性、DLSS Ray Reconstruction同等性;
- stochastic sampling、temporal history、spatial filteringのproduction default昇格。

## 計画規模の見直し

維持しているroadmap自体は縮小しないが、実装は段階化する。このbranchはweighted-signal/moments contractとdiagnosticsから開始する。新しいspatial filter、大規模RenderGraph refactor、dynamic object-motion redesignはこの最初の作業単位では行わず、diagnostic gateの証拠を必要とする。

## 2026-08-24: Producer/consumer transition監査

- `ReflectionEvaluatePass`は未加重`ReflectionEvaluatedRadiance`とweighted `ReflectionSpecularEstimate`を同時生成する。weighted targetは同じincident sampleを使い、visible-surface Fresnel、GGX distribution/geometry、cosine、directional-PDF compensationを含む。
- 現行`TemporalReflectionPass`は未加重Evaluated Radianceだけを読む。現行`LightPass`はそのresolved formにdistance fade、`(1 - visible roughness)`、user intensity、`FresnelSchlickRoughness`を適用し、既にdeterministic Specular IBLを含むcolorへ加算する。
- weighted transitionではvisible-roughness multiplierとFresnel multiplierを再利用しない。またenvironment miss estimateをdeterministic Specular IBLへ重ねて加算しない。
- 判断: future default-off weighted pathはdeterministic `iblSpecular`から`ReflectionResolvedSpecularEstimate`へblendする。user intensityと保持するhit-distance policyがblendを制御する。finite hit distanceはIBLへfade backしてよいが、environment missはfull estimator replacementの対象に残す。
- 2-channel luminance momentsの候補contractを定義した。first/second momentはresolved estimateと同じreprojection、acceptance、history weight、reset、ping-pong ownershipに従う。rejectされたhistoryはcurrent sampleから初期化する。varianceは`max(M2 - M1 * M1, 0)`でありradiance alphaへ格納しない。

この監査ではruntime pathまたはresourceを変更していない。次の作業単位はadaptive policy実装より前のmoments format/range測定とdiagnostic exposureである。

## 2026-08-25: Moments range／precision監査

rendererを再実行せず、既存controlled-scene weighted-estimator reportを確認した。

| 条件 | Window | Maximum luminance | Maximum squared luminance |
| --- | ---: | ---: | ---: |
| roughness `0.0`, metallic | 256 frames | `0.307697` | `0.094677` |
| roughness `0.35`, metallic | 1024 frames | `7.82340` | `61.2056` |
| roughness `1.0`, metallic | 1024 frames | `5.29550` | `28.0423` |

測定値はすべてFP16 range内だが、rangeは決定条件ではない。`M2 - M1^2`は桁落ちに敏感であり、deterministic roughness `0.0` mirror controlはnear zeroを維持する必要がある。したがって初期diagnostic historyは`R32G32_FLOAT`（`.x = M1`、`.y = M2`）を使う。FP16はpaired precision比較をgateとする将来のmemory/bandwidth最適化として残す。

この作業単位ではcode／shaderを変更していないためbuildは不要だった。次はweighted resultをLightPassへ接続せず、独立ping-pong moments resourceとdebug exposureを追加する。

## 2026-08-25: Resolved weighted estimate／moments diagnostics

- `TemporalReflectionPass`へ2つのdiagnostic MRT outputを追加した。`R16G16B16A16_FLOAT`のping-pong `ReflectionResolvedSpecularEstimate`と、`R32G32_FLOAT`のping-pong `ReflectionSpecularMoments`である。
- 両outputは既存motion-vector reprojection、bounds check、depth/normal acceptance、history reset、accepted history weight、history role exchangeを共有する。history reject時はcurrent weighted sampleからresolved estimateとmomentsを初期化する。
- `Resolved Specular`／`Specular Variance` UI debug viewと、`resolved-specular-estimate`／`specular-variance` capture selectorを追加した。
- variance viewは`max(M2 - M1^2, 0)`を計算し、表示時だけ`v / (1 + v)`を適用する。保存momentsはlinearかつunmappedのままである。
- LightPassはlegacy未加重`ReflectionResolvedRadiance`へ接続したままであり、production compositionとdefaultは変更していない。

Validation:

- Debug x64は影響するC++と全HLSLをerror 0件でrebuildし、既存vcpkg重複import warningだけを報告した。
- roughness `0.35`の64-frame runtime smokeはD3D12 error 0件、既知committed-buffer warning 3件で完了した。
- 120-frame warm-up後の`Specular Variance` captureはD3D12 error 0件で完了した。目視ではrough sphereに強いvarianceがあり、mirror側はほぼblackとなる期待したorderingを再現した。

生成JSON、PNG、logはuntrackedのままとする。この結果はdiagnostic observabilityを確立するが、adaptive filterの有効性はまだ示さない。

## 2026-08-25: Linear-HDR moments report／roughness gate

- HDR diagnostic readbackとschemaをversion 3からversion 4へ拡張した。reportは、debug表示用mappingを保存linear値へ適用せず、`ReflectionResolvedSpecularEstimate`のtemporal statisticsと`ReflectionSpecularMoments`のsummaryを含む。
- moments summaryはROI／frame window全体のmean `M1`、mean `M2`、mean `max(M2 - M1^2, 0)`、maximum estimated varianceを記録する。これはtemporal estimatorのmetadataであり、physical ground truthではない。
- stochastic sampling有効、temporal weight `0.9`、固定camera／scene、48x48 metallic sphere ROI、32-frame warm-upの条件で、deterministicな64／256-frame測定を実施した。独立した64-frame processと256-frame processは、3 ROIすべてで先頭64-frameのresolved estimate／moments sampleが完全一致した。

| Roughness | 64-frame mean estimated variance | 256-frame mean estimated variance | 256-frame resolved temporal variance | 256-frame resolved frame-difference p99 |
| ---: | ---: | ---: | ---: | ---: |
| `1.0` | `0.0781654` | `0.0814923` | `0.00418381` | `0.117602` |
| `0.35` | `0.00605023` | `0.00611414` | `0.000318847` | `0.0111988` |
| `0.0` | `1.00777e-10` | `1.00777e-10` | 約`0` | `0` |

observability gateはpassした。mirror controlは実質zeroを維持し、roughness `0.35`はnonzero variance、roughness `1.0`は最大のROI-average varianceを示した。Debug x64はerror 0件、既存vcpkg重複import warningのみで完了した。6つのruntime measurementはすべてD3D12 error 0件、同じ既知committed-buffer warning 3件で完了した。

この結果が検証するのはdiagnostic orderingとdeterministic measurement contractだけである。production filtering policy、scene generalization、physical correctness、最適なvariance thresholdは確立していない。次の限定作業単位では、同じpaired 64／256-frame条件を使い、1つのdefault-off adaptive policyを固定temporal accumulationと比較してよい。

## 2026-08-25: 限定variance-guided temporal実験

weighted estimatorだけを対象とするdefault-off policyを1つ実装した。accepted historyでprior relative varianceを`saturate(max(M2 - M1^2, 0) / max(M2, 1e-6))`として計算し、weighted estimatorのeffective history weightを設定base weightから`0.98`へ補間する。resolved weighted estimateとmomentsはこのeffective weightを共有する。legacy未加重resolved radianceとLightPassは変更しない。policyは`Variance-Guided Temporal`および`-ReflectionVarianceGuidedTemporal`で有効化できる。

paired A/Bはstochastic sampling、base weight `0.9`、32 warm-up frames、固定camera／scene／ROI、同一current sample sequenceを使用した。下表は固定weight Aに対するadaptive Bの変化である。

| Roughness | Window | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | 判定 |
| ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | 64 | `+11.82%` | `-75.40%` | `-70.66%` | Reject: 短時間mean shift |
| `0.35` | 64 | `+2.13%` | `+25.99%` | `-16.99%` | Reject: variance増加 |
| `0.0` | 64 | `0%` | `0%` | `0%` | Mirror control維持 |
| `1.0` | 256 | `+7.10%` | `-74.33%` | `-73.60%` | Reject: mean shift |
| `0.35` | 256 | `+3.27%` | `+78.92%` | `-18.07%` | Reject: variance増加とmean shift |
| `0.0` | 256 | `0%` | `0%` | `0%` | Mirror control維持 |

初期formulaは昇格しない。stronger historyがrough surfaceのframe differenceを低減できる一方、variance magnitudeだけではeffective history weightを選べず、convergence stateとweight stabilityも必要であることを示した。実装は限定diagnostic experimentとしてdefault-offのまま保持する。すべてのpaired runでcurrent sampleは一致し、D3D12 error 0件、processごとに既知committed-buffer warning 3件だった。

次はeffective weighted history weightを表示または記録し、weighted historyをLightPassへ接続する前に限定confidence／clamping ruleを評価する。これはpolicy refinement gateであり、Hybrid Reflection roadmapの縮小ではない。

## 2026-08-25: Effective-weight診断／bounded confidence policy

- schema version 8のpolicy-weight診断を追加した。HDR captureはvisible PBR paramsもreadbackし、frameごとおよびaggregate reportへpolicy-selected weightのmean、standard deviation、minimum、p95、p99、maximumを記録する。
- report値は保存momentsとvisible roughnessから、次のaccepted same-pixel history weightを予測する。motion reprojectionは再現しないため、解釈は固定camera ROI workflowへ限定する。
- weight診断により不採用continuous formulaの原因を説明できた。roughness `1.0`はmean `0.9665`、p99 `0.9786`だった。roughness `0.35`はmean `0.9139`に留まる一方、p99 `0.9719`の局所outlierを含んだ。mirror roughness `0.0`はbase `0.9`を維持した。
- thresholdだけのv2（`relative variance >= 0.5`で`0.94`を選択）はroughness `1.0`の効果を維持したが、roughness `0.35`の少数pixel切替により64-frame temporal varianceが`28.51%`増加した。
- bounded v3はvisible roughness `>= 0.75`を追加する。2つのconfidence条件を両方満たす場合だけ`max(base_weight, 0.94)`を選び、それ以外はbase weightを維持する。

base weight `0.9`、32 warm-up frames、固定48x48 metallic ROI、同一current sample sequenceによるpaired v3結果:

| Roughness | Window | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | Policy-weight mean | 判定 |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | 64 | `-0.044%` | `-49.46%` | `-39.91%` | `0.93996` | Pass |
| `0.35` | 64 | `0%` | `0%` | `0%` | `0.9` | Pass、unchanged control |
| `0.0` | 64 | `0%` | `0%` | `0%` | `0.9` | Pass、mirror control |
| `1.0` | 256 | `-0.323%` | `-43.90%` | `-39.88%` | `0.93997` | Pass |
| `0.35` | 256 | `0%` | `0%` | `0%` | `0.9` | Pass、unchanged control |
| `0.0` | 256 | `0%` | `0%` | `0%` | `0.9` | Pass、mirror control |

Debug x64と影響HLSLはerror 0件、既存vcpkg重複import warningのみで完了した。v3 runtime runはすべてD3D12 error 0件、processごとに既知committed-buffer warning 3件で完了した。

限定主張: 評価したcontrolled high-roughness metallic ROIにおいて、default-off v3 policyはlinear-HDR temporal varianceとframe-difference p99を低減し、256-frame mean変化を1%未満に維持した。gate以上の中間roughness、dielectric surface、texture付きproduction asset、motion、Lit compositionの挙動は確立していない。これらを次のgeneralization／composition gateとする。

## 2026-08-25: Controlled material generalization gate

Estimator Test sceneはroughness `0.0`、`0.05`、`0.15`、`0.35`、`0.6`、`1.0`をmetallic／dielectric rowに持つ。正確な`0.75` sphereはないため、roughness `0.6`をthreshold直下control、roughness `1.0` dielectricをmaterial横断のactive条件として使用した。

paired測定は同じbase weight `0.9`、32-frame warm-up、固定48x48 ROI、同一current sampleを使用した。

| 条件 | Window | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | Policy-weight mean | 判定 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| roughness `1.0`, dielectric | 64 | `-0.135%` | `-49.56%` | `-39.94%` | `0.93994` | Pass |
| roughness `0.6`, metallic | 64 | `0%` | `0%` | `0%` | `0.9` | Pass、below-gate control |
| roughness `0.6`, dielectric | 64 | `0%` | `0%` | `0%` | `0.9` | Pass、below-gate control |
| roughness `1.0`, dielectric | 256 | `-0.181%` | `-43.75%` | `-39.98%` | `0.93998` | Pass |
| roughness `0.6`, metallic | 256 | `0%` | `0%` | `0%` | `0.9` | Pass、below-gate control |
| roughness `0.6`, dielectric | 256 | `0%` | `0%` | `0%` | `0.9` | Pass、below-gate control |

全processはD3D12 error 0件、各processで既知committed-buffer warning 3件により完了した。限定主張をmetallic／dielectric high-roughness controlへ拡張し、明示的なbelow-threshold挙動を確認した。texture付きproduction assetまたはmotion上の性能はまだ確立していない。

次は既知のDamagedHelmet material-region ROI 2箇所で同じfixed／bounded比較を行い、Lit composition変更前に限定主観captureを実施する。

## 2026-08-25: DamagedHelmet development gate

確立済みDamagedHelmet 2 ROIで、32-frame warm-up後にfixed／bounded v3を64 frames比較した。current sample sequenceは一致した。

| ROI | Center roughness / metallic | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | Policy-weight mean | 判定 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `rearward_surface` | `0.133 / 0.953` | `-0.012%` | `-0.12%` | `-0.02%` | `0.90258` | Fail: 実用効果がほぼない |
| `underside_pipes` | `0.216 / 0.992` | `-0.037%` | `-0.33%` | `-0.55%` | `0.90339` | Fail: 実用効果がほぼない |

controlled high-roughness policyは、texture付きROIの大部分が明示的roughness `0.75` gate未満であるため、既知production-asset noise regionへgeneralizeしない。このformulaでは64-frame development gateですでにtarget領域で実質inactiveと判明したため、256-frame標準runは実施しない。experimentはdefault-offのためproduction regressionではない。

roughness thresholdを単純に下げて対応しない。threshold-only v2はroughness `0.35`で少数pixelの切替がvarianceを増加させることを既に示した。次のcontract stepは、policy activationを時間方向に安定化するpersistent confidence／history signalである。これは以前から予約していたconfidence extensionであり、controlled sceneとDamagedHelmetの測定結果により導入根拠が成立した。

## 2026-08-26: Persistent confidence contract

- 独立ping-pong `ReflectionSpecularConfidence` resourceを`R16_FLOAT`で採用する。confidenceはscalar metadataのままとし、radiance alphaまたは2-channel moments resourceへ格納しない。
- confidenceはweighted estimatorのreprojection、depth／normal acceptance、reset、ownership、post-submit role exchangeを共有する。rejectまたはresetされたhistoryではconfidenceをzeroへ初期化する。
- accepted historyではprior relative varianceをthreshold `0.5`でbinary indicatorへ変換し、固定`0.9` confidence-history weightを適用する。activationには継続的なevidenceが必要となり、evidence消失時も対称的に減衰する。
- 更新後confidenceを`smoothstep(0.5, 0.9, confidence)`へ通し、設定base history weightと`max(base, 0.94)`の間を選択する。resolved weighted estimateとmomentsは同じeffective weightを共有する。legacy未加重resolved radianceは設定base weightを維持する。
- candidateからroughness `0.75` gateを撤去する。persistent confidenceの目的は、v2で測定した瞬間的な少数pixel切替を防ぎながら、低roughness production-asset noiseを対象に含めることである。

この作業単位ではsemanticsだけを固定する。次はresource、MRT output、debug／report observabilityを実装し、paired qualityを主張する前にconfidence rise／decayを検証する。

## 2026-08-26: Persistent confidence実装／contract gate

- ping-pong `ReflectionSpecularConfidence`を`R16_FLOAT`で追加した。transient registration、RTV／SRV allocation、RenderGraph ownership、history binding、reset／release処理、既存reflection history stateによるpost-submit role exchangeを含む。
- Temporal Reflectionを6 MRTへ拡張した。shaderはtarget 5へconfidenceを書き、space 17からreprojected confidenceを読み、roughness gateを撤去し、文書化したpersistent-confidence updateとsmooth bounded weightを適用する。
- generic fullscreen pipeline definitionのadditional render targetを4から5へ拡張した。他のfullscreen passにはtargetを追加していない。
- `Specular Confidence` UI debug viewを追加した。whiteは継続的high-variance evidence、blackはreject／reset historyを示す。
- `R16_FLOAT` HDR readbackとschema version 9を追加した。reportはschema v8のstatic same-pixel予測ではなく、current motion-reprojected confidenceと適用effective-weight分布を含む。

controlled 64-frame contract gateは32-frame warm-up、stochastic sampling、base weight `0.9`、固定48x48 metallic ROIを使用した。

| Roughness | Confidence mean | Confidence p95 | Last-frame mean | Effective-weight mean | 判定 |
| ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | `0.9828` | `0.9956` | `0.9944` | `0.93984` | Pass: persistent activation |
| `0.35` | `0.0133` | `0` | `0.0201` | `0.90043` | Pass: sparse spikeは広範囲をactivateしない |
| `0.0` | `0` | `0` | `0` | `0.9` | Pass: mirror control |

Debug x64と影響HLSLはerror 0件、既存vcpkg重複import warningのみで完了した。runtime reportはD3D12 error 0件、processごとに既知committed-buffer warning 3件で完了した。これはresource ownershipとsteady confidence separationを検証するが、paired image-quality改善または入力変化時のconfidence decayはまだ検証していない。

次はcontrolled roughness条件でfixed／confidence-guided paired 64-frame quality gateを実施し、その後explicit confidence decayとDamagedHelmet development testへ進む。

## 2026-08-26: Persistent confidence paired quality gate

fixed-weight／confidence-guided runは、同一sampling-frame sequence、32-frame warm-up、base history weight `0.9`、stochastic sampling、同一48x48 metallic ROIを使用した。比較signalは`ReflectionResolvedSpecularEstimate`であり、legacy未加重resolved radianceは意図どおり変更しない。

| Roughness | Frames | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | Confidence mean | Effective-weight mean | 判定 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `1.0` | `64` | `+0.0886%` | `-49.62%` | `-39.91%` | high | `0.94`付近 | Pass: target varianceを低減 |
| `0.35` | `64` | `+0.2590%` | `-2.38%` | `-2.20%` | low | `0.9`付近 | Pass: bounded response |
| `0.0` | `64` | `0%` | `0%` | 対象外 | `0` | `0.9` | Pass: mirror control不変 |
| `1.0` | `256` | `-0.2873%` | `-43.95%` | `-39.87%` | `0.99170` | `0.93993` | Pass: standard frame数で再確認 |
| `0.35` | `256` | `+0.3373%` | `+2.10%` | `-2.45%` | `0.02141` | `0.90072` | Pass with limitation: 小幅variance悪化 |
| `0.0` | `256` | `0%` | `0%` | 対象外 | `0` | `0.9` | Pass: mirror control不変 |

全paired sample sequenceは一致した。6本の256-frame processはD3D12 error 0件、processごとに既知committed-buffer warning 3件で完了した。この結果はcontrolled sceneでの選択的persistent activationを支持する。confidence decay、DamagedHelmetでの有効性、Lit品質、estimator correctness、production readinessはまだ確立しない。roughness `0.35`のvariance増加はframe-difference改善で隠さず、制限として保持する。

次はexplicit confidence decayを検証し、その後、確立済みDamagedHelmet ROIをproduction-asset development gateとして実行する。

## 2026-08-26: DamagedHelmet persistent-confidence gate

確立済みtextured-asset ROIで、sampling-frame sequenceが一致するfixed／confidence-guided process、32 warm-up frames、stochastic sampling、base weight `0.9`を使用した。

| ROI | Frames | Mean変化 | Temporal variance変化 | Frame-difference p99変化 | Confidence mean | Effective-weight mean |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | `64` | `-0.0095%` | `-5.03%` | `-1.28%` | `0.06214` | `0.90189` |
| `underside_pipes` | `64` | `-0.0130%` | `-20.60%` | `-3.20%` | `0.10536` | `0.90322` |
| `rearward_surface` | `256` | `-0.0078%` | `-4.10%` | `-1.34%` | `0.06393` | `0.90199` |
| `underside_pipes` | `256` | `-0.0207%` | `-17.78%` | `-3.35%` | `0.10948` | `0.90339` |

8 processすべてD3D12 error 0件、processごとに既知committed-buffer warning 2件で完了した。persistent confidenceは撤去したroughness gateを越えて既知textured-asset noise region 2箇所へgeneralizeしたが、効果はboundedである。standard frame数でrearward ROIの改善は約4%であり、どちらのROIもcontrolled roughness `1.0`ほど強くactivateしない。これはweighted estimatorのlinear-HDR診断結果であり、Litの知覚品質またはproduction denoiser readinessを主張しない。

診断applicationはcomplete reportを書いた後も自動終了しない。runnerはreport完成を待ち、自身が起動したprocessだけを停止するよう運用した。このautomation挙動はrendering結果と分離して記録する。

次はexplicit confidence decay／reset transition gateを実行し、このpolicyをLit主観比較へ進められるか判断する。

## 2026-08-26: Confidence decay／reset transition gate

diagnostic-only measurement transitionを追加した。`-ReflectionConfidenceForceStableAfterMeasurementFrames N`は`N` capture frames後にconfidence variance indicatorをzeroへ固定し、radiance、sampling、history validity、acceptance pathは変更しない。`-ReflectionHistoryResetAfterMeasurementFrames N`は`N` capture frames後にreflection historyを明示invalidateする。schema version 10は両transition contractを記録する。どちらも通常描画のdefaultへ影響しない。

decay gateはcontrolled roughness `1.0` metallic ROI、32 warm-up frames、96 measurement framesを使用した。stable evidenceはmeasurement frame 31の後に開始した。confidenceはframe 31の`0.993395`からframe 32の`0.893947`、frame 38の`0.473901`へ低下した。effective weightはtransitionから6 frames後のframe 38で正確にbase `0.9`へ戻り、temporal acceptanceは`1.0`を維持した。frame 95のconfidenceは`0.001147`である。これによりaccepted-history decayをreset／rejectionから分離して確認した。

reset gateは同じcontrolled ROIを使用し、16 measurement frames後にresetした。confidenceはframe 15の`0.978657`からframe 16の`0`へ変化し、effective weightは`0.939883`から`0.9`へ戻り、reset frameのacceptanceはzeroになった。次frameはzero confidenceのaccepted historyを再開し、その後frame 47でconfidence `0.904974`まで再構築した。これによりreset ownershipを自然decayと独立に検証した。

Debug x64と影響HLSLはerror 0件、既存vcpkg重複import warningのみでbuildできた。両runtime gateはD3D12 error 0件、既知committed-buffer warning 3件で完了した。診断executableはreport完成後、runnerが自身の起動processを停止する必要が引き続きあった。

persistent-confidence lifecycle gateは完了した。次はbounded Lit主観比較を準備し、静止noise、motion response、測定済みweighted-estimator改善がfinal compositionで知覚できるかを確認する。

## 2026-08-26: Lit composition gate修正

最初のfixed／confidence Lit captureは、静止、移動中、方向反転、停止後の4 checkpointすべてでPNGがbyte-identicalだった。これは同等品質の主観証拠ではなく、その時点の接続では期待される結果である。persistent confidenceは`ReflectionResolvedSpecularEstimate`だけへ作用し、LightPassは固定weightのlegacy `ReflectionResolvedRadiance`を消費していた。

そこでdefault-off policyを拡張し、`ReflectionResolvedRadiance`、`ReflectionResolvedSpecularEstimate`、`ReflectionSpecularMoments`へ同じeffective history weightを使用する。BRDF-weighted diagnostic estimatorをLightPassへ入力せず、Fresnelまたはfinal contribution weightingをLightPassから移動しない。既存未加重radiance境界のaccumulation rateだけをconfidenceで制御する。policy無効時はbase-weight pathを維持する。

次はrebuild後、未加重resolved radianceについてcontrolled／DamagedHelmet数値回帰を再実行し、Lit suiteを再生成する。最初のbyte-identical captureは主観gate結果として採用しない。

修正後の64-frame paired regressionはmatching sample sequenceで完了し、次の結果となった。

| ROI | 未加重mean変化 | 未加重temporal variance変化 | 未加重frame-difference p99変化 | Control結果 |
| --- | ---: | ---: | ---: | --- |
| controlled roughness `1.0` | `+0.2702%` | `-49.52%` | `-39.69%` | strong activation |
| controlled roughness `0.0` | `0%` | `0%` | `0%` | mirror control不変 |
| DamagedHelmet `rearward_surface` | `+0.0020%` | `-6.76%` | `-4.21%` | bounded improvement |
| DamagedHelmet `underside_pipes` | `+0.0233%` | `-27.31%` | `-10.24%` | bounded improvement |

最終8 processはすべてD3D12 error 0件だった。controlled runは既知committed-buffer warning 3件、DamagedHelmet runは2件を維持した。最初のpipes B attemptでは、新規作成された0-byte reportをrunnerが完成と誤認してprocessを停止した。完成条件をvalid JSONかつ64 framesへ修正して正常に再実行した。これはautomation raceでありrendering failureではない。

静止、移動中、方向反転、停止後を対象とする4-checkpointの拡大DamagedHelmet Lit capture planと日英suiteを追加した。composition修正後は4枚すべてのB captureが対応A captureと異なり、両capture runはD3D12 error 0件、既知warning 2件だった。これによりpolicyがLit compositionへ到達したことは確立するが、主観品質はまだ主張しない。

user reviewにより、最初のcamera-distance scale `0.5`では下面pipeは見えるが、確立済みrearward-surface regionが画像上端付近で欠けることが判明した。Lit suiteはscale `0.6`、元のlinear sizeのおよそ1.67倍へ変更した。再撮影frameではsmooth crown panel、それに隣接するrearward surface、下面pipeを同時に確認できる。static criterionも曖昧な「既知noise region」ではなく両領域を明記した。

validation runnerはcapture planを書いた後にprocessが終了することを前提としない。variantごとに全planned outputがnon-emptyかつvariant開始後のwrite timeを持つまで待ち、自身が起動したprocessだけを停止する。これにより古いcapture fileを完成と誤認せず、観測されたpost-capture process timeoutへ対応した。
