# Hybrid Reflection Comparison Contracts

## 目的

本文書は、Raster、Hybrid Reflection、将来のPath Tracing（PT）、将来のDLSS Ray Reconstruction（RR）を比較するときのsignal、scene state、presentation条件、metric、主張範囲を固定する。現在未実装のPT／RR backendを実装済みと扱わず、異なる意味のbufferを同じ画像として比較しないことが目的である。

## 比較境界

| Boundary | Current signal | Semantics | 許可する比較 |
|---|---|---|---|
| Current reflection | `ReflectionEvaluatedRadiance.rgb` | linear HDR、current-frame、temporal前、LightPassのdistance／visible roughness／intensity／Fresnel weighting前 | sampling、hit/miss、current estimator、将来PTの同等one-bounce signal |
| Resolved reflection | `ReflectionResolvedRadiance.rgb` | linear HDR、temporal後、最終weighting前。未加重semanticsを維持 | temporal／denoise policy同士。current reflectionとの比較では処理差を明記 |
| Spatial reflection | `ReflectionDenoisedRadiance.rgb` | default-off spatial処理後、最終weighting前。temporal historyではない | spatial policy同士。filter OFFとのpaired比較 |
| Final Lit | `LightPass.RenderTarget` | reflection contributionを含むlinear HDR scene color、tone map前 | Raster／Hybridの実用画像比較。reflection以外のlightingが同一であること |
| Presentation | tone map／upscale後のoutput | display-referred出力 | 最終知覚品質のみ。radiance、bias、energyの比較には使用しない |

`ReflectionSpecularEstimate`と`ReflectionResolvedSpecularEstimate`はweighted estimator診断であり、unweighted radianceではない。`ReflectionRayColor`はhit albedoであり、reflection radianceではない。これらを上表のradiance境界として比較してはならない。

## Mode matrix

| Mode | Current status | Reflection-boundary comparison | Final-Lit comparison |
|---|---|---|---|
| Raster baseline | 実装済み | Hybridと同一のray-radiance signalを持たないため直接比較不可 | Hybrid OFFの`LightPass.RenderTarget`をbaselineとする |
| Hybrid deterministic | 実装済み | `ReflectionEvaluatedRadiance`。stochastic OFF、history weight `0.0` | Hybrid ON、実験filter OFF |
| Hybrid stochastic current | default-off診断 | `ReflectionEvaluatedRadiance`または`ReflectionSpecularEstimate`を名称付きで選択 | stochastic ON、history weight `0.0` |
| Hybrid resolved | default-off診断 | `ReflectionResolvedRadiance`または`ReflectionDenoisedRadiance`を名称付きで選択 | temporal／spatial設定をreportへ完全記録 |
| Path Tracing | 未実装 | 将来、bounce範囲とunweighted／weighted semanticsが一致するsignalだけ比較 | 同一scene／camera／exposureのPT Lit output |
| DLSS RR | 未実装 | backendが要求／生成するsignalを別contractで固定するまで比較不可 | RR有無の同一render path paired比較。SR差と混同しない |

PTの多数sample画像を自動的にground truthと呼ばない。integrator、bounce数、light transport、material model、environment、clamp、sample countが宣言され、比較対象と意味が一致する場合だけphysical reference候補とする。現在のestimatorを多数sample平均したものは`Current-Estimator Mean Baseline`であり、physical GTではない。

## 固定scene state

paired比較では以下を同一にする。

- scene asset、node visibility、world transform、animation time;
- camera mode、view／projection、FOV、near／far、Arcball pivot／distance;
- render resolution、output resolution、viewport、crop／ROI;
- light、IBL、emissive、material、reflection distance／intensity設定;
- exposure、tone-map mode、HDR／SDR output mode;
- frame timeline、warm-up、measurement range、history reset point;
- stochastic sample index／sequence。異なるestimator間で同一乱数が意味を持たない場合は、その制限を記録する;
- temporal、spatial、upscaler、RRの有効状態とpreset。

cameraまたはobject motionを含む比較は、同じcontinuous timelineを使用する。静止画だけでtemporal stability、ghosting、settlingを合格判定しない。

## Exposureとpresentation

linear-HDR metricはtone map、display transfer、UI overlay、presentation scaling前の対応するsignalで計算する。Final LitのHDR比較では同じ固定exposureを使う。auto exposureを使用する場合、そのrunは別profileとし、exposure値またはexposure textureの時系列を保存する。

SDR／HDR10 screenshotは主観評価用であり、linear radianceのmean、variance、bias、energy保存を証明しない。DLSS SRまたは別upscalerを有効にした画像はnative-resolution比較から分離し、render／output resolutionを明記する。

## Metric contract

| Metric | Domain | 用途／制限 |
|---|---|---|
| mean luminance、mean relative difference | linear HDR、対応ROI | 平均輝度維持。zero近傍ROIではrelative値を単独使用しない |
| variance、standard deviation、coefficient of variation | linear HDR、時間方向 | static noise。sampling sequenceとframe範囲を固定 |
| frame absolute difference mean／p95／p99 | linear HDR、時間方向 | flicker／tail。camera motion区間とstatic区間を分離 |
| RMSE | 同じsemantic boundaryのlinear HDR | reference名とsample条件を必ず記録 |
| T50／T90／T95 | linear HDR ROI response | motion停止後のsettling。十分なsignal amplitudeがある場合だけ有効 |
| hit／accept／reject rates | diagnostic metadata | 原因説明用。画質またはcorrectness確率ではない |
| GPU time、memory | 宣言hardware／resolution | production cost。未測定ならperformanceは`NOT CLAIMED` |
| 主観A/B | 同じpresentation条件 | 知覚品質。差が見えない結果はnon-regressionのみ |

## Capture／report metadata

comparison reportは最低限、commit、scene、mode、signal boundary、resolution、camera、exposure、frame range、warm-up、sample sequence、history reset、temporal／spatial／upscaler設定、ROI、metric domainを保存する。A/B labelだけで設定を推測させない。

## Claim boundary

Current codeで主張できるのはRaster final-Lit baseline、Hybridの各実装済みsignal、同一条件のdiagnostic A/Bまでである。PTおよびDLSS RRの品質、correctness、speedup、比較可能性は`NOT CLAIMED`とする。DLSS SR outputをRR outputとして扱わない。

このbranchはcomparison contractを固定する。PathTracing pass、RR backend、Streamline SDK型、large RenderGraph refactor、production default変更は行わない。
