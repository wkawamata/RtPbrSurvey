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
