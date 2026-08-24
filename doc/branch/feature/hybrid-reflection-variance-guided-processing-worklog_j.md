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

