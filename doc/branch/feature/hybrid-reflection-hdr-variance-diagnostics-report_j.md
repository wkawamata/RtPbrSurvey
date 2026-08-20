# Hybrid Reflection HDR Variance Diagnostics Phase 1 最終レポート

## 1. 結論

Phase 1では、Hybrid Reflectionの時間方向の不安定さをdisplay-space PNGではなくlinear-HDR signal domainで測定する診断経路を構築し、既存のdefault-off Surface Variance Filterをpaired条件で評価した。

評価対象のDamagedHelmet 2 ROIでは、filterはResolved Radianceのtemporal varianceを約75～77%低減し、256-frameのROI measurement mean変化を0.2%未満に抑えた。一方、High-SPP Current-Estimator Mean Baselineに対するpixel単位RMSEは増加した。

したがって、Phase 1の最終判断は次のとおりとする。

- filterのvariance低減効果は、評価した2 ROIと固定条件で確認した。
- ROI全体の平均輝度維持だけでは、局所signalまたはspatial detailの維持を保証できない。
- variance低減だけを根拠にproduction filterへ昇格させない。
- Surface Variance Filterはdefault-offの限定診断実装として維持する。
- estimator correctnessとphysical referenceへの収束はPhase 1では主張しない。

## 2. Phase 1の目的と主張可能範囲

目的は、現在の近似estimatorとtemporal/filter処理を変更する前に、linear HDRでvariance、時間差分、history validity、現在のestimator平均への距離を再現可能に測定することである。

Phase 1で主張できること:

- 現在の近似estimatorに対するvariance低減;
- 固定measurement windowにおけるmean変化;
- Current-Estimator Mean Baselineに対するRMSE;
- paired A/Bのsample/temporal index一致;
- 評価したROIと設定に限定したfilterの利点と副作用。

Phase 1で主張しないこと:

- BRDF積分としての正しさ;
- unbiased estimatorであること;
- energy conservation;
- physical ground truthへの一致;
- production denoiser readiness;
- 未評価scene、material、camera、motion条件へのgeneralization。

## 3. 測定contract

| Level | 用途 | Phase 1での扱い |
| ---: | --- | --- |
| 64 frames | 開発中の高速gate | 両ROIで実施 |
| 256 frames | 標準PR評価 | 両ROIで実施 |
| 1024 frames | mean drift、rare firefly、bias傾向の追加監査 | 今回は未実施。異常が見つかった場合のみ実施 |

paired A/Bでは以下を固定した。

- variantごとに新規processから開始し、sample/historyを初期化;
- cameraとanimation状態;
- 32 warm-up frames;
- measurement frame範囲;
- stochastic sampling有効;
- temporal history weight `0.9`;
- render-space ROI;
- filter以外のrenderer設定。

A/B reportにsampling frame indexとtemporal frame indexを記録し、sequence一致を機械的に検証した。256-frame評価では両ROIともsequenceが一致し、Evaluated Radianceのmean差は0だった。

## 4. 診断実装

Phase 1で以下を追加した。

- `R16G16B16A16_FLOAT` linear-HDR ROI readback;
- 同一submit frameの`ReflectionEvaluatedRadiance`、現在の`ReflectionResolvedRadiance` write slot、`ReflectionRayHit`取得;
- warm-up/measurement分離と連続frame scheduling;
- JSON report出力;
- filter off/on paired orchestrator;
- pixel-temporal statistics;
- High-SPP Current-Estimator Mean BaselineとRMSE series。

reportには以下を含める。

- temporal mean、variance、standard deviation;
- coefficient of variation。meanの絶対値が`1e-6`以下の場合は`null`;
- frame間luminance絶対差のmean、p95、p99;
- maximum luminance。firefly確認用の補助指標;
- hit率;
- temporal acceptance率;
- depth reject率;
- normal reject率;
- Current-Estimator Mean Baselineに対するEvaluated/Resolved RMSE;
- frameごとのRMSE series。

## 5. 固定ROI

1920x1080 DamagedHelmet render-spaceで以下を評価した。

| ID | Rectangle | 対象 |
| --- | --- | --- |
| `rearward_surface` | x `895`, y `278`, width `75`, height `85` | 後頭部寄りのnoisy surface |
| `underside_pipes` | x `805`, y `585`, width `125`, height `135` | 下面pipe領域 |

## 6. 256-frame paired結果

### 6.1 Varianceとmeasurement mean

| ROI | Resolved variance off | Resolved variance on | Variance低減 | Measurement mean差 | Frame差分p99 off/on |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | `3.8915666e-5` | `8.8329177e-6` | `77.3024%` | `0.1842%` | `0.0135398 / 0.0060411` |
| `underside_pipes` | `4.2294466e-5` | `1.0476135e-5` | `75.2305%` | `0.1021%` | `0.0077752 / 0.0034011` |

この結果は、評価条件内でfilterが時間方向のvarianceと大きなframe差分を低減したことを示す。

### 6.2 Current-Estimator Mean Baseline RMSE

High-SPP Current-Estimator Mean Baselineは、measurement window内の`ReflectionEvaluatedRadiance`をpixelごとに算術平均したものである。物理GTではなく、現在の近似estimator平均への収束とsignal preservationを測るin-sample baselineである。

| ROI | Baseline samples | Evaluated RMSE | Resolved RMSE off | Resolved RMSE on | Filter-on RMSE変化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rearward_surface` | 256 | `0.0284464` | `0.0062587` | `0.0132367` | `+111.4950%` |
| `underside_pipes` | 256 | `0.0285282` | `0.0065240` | `0.0068497` | `+4.9926%` |

filterはtemporal varianceを低減した一方、両ROIでraw current-estimator meanへのpixel単位RMSEを増加させた。rearward surfaceの大きな増加は、ROI全体meanでは隠れるlocal meanまたはspatial detailの変化と整合する。ただし、このbaselineはphysical referenceではないため、物理的biasを証明するものではない。

## 7. Acceptance gate

| Gate | 判定 | 根拠または制限 |
| --- | --- | --- |
| 64/256-frame contract | PASS | 64は開発、256は標準、1024は条件付きと固定 |
| paired sample/temporal sequence | PASS | 両ROIの256-frame A/Bで一致 |
| 2 ROI評価 | PASS | `rearward_surface`、`underside_pipes`完走 |
| required rates | PASS | hit、accept、depth reject、normal rejectをJSONへ出力 |
| baseline terminology | PASS | `physicalReference: false`を出力し、物理GTではないと明記 |
| Debug x64/HLSL build | PASS | 2026-08-21最終build成功 |
| D3D12 Debug Layer | PASS WITH KNOWN WARNINGS | error 0件。既知のcommitted-buffer initial-state warning 2件。新規warningなし |
| 日英worklog | PASS | 実装ログ7区分が対応し、結果とclaim boundaryが一致 |
| production filter readiness | NOT CLAIMED | RMSE増加があり、default-offを維持 |
| estimator correctness | NOT CLAIMED | Phase 2でBRDF/PDF/throughputを監査 |

build時には既存のvcpkg target重複import warningが1件表示された。D3D12 runtime warningとは別であり、このbranchによる新規warningではない。

## 8. 1024-frame監査の扱い

1024-frame監査は実行しない。256-frameのpaired sequenceは安定し、両ROIでvariance低減とmean差を再現できた。今回の重要な未解決点は長時間統計の不安定さではなく、filter-on時のpixel RMSE増加である。

次の場合に限り1024-frame監査を追加する。

- mean driftの疑い;
- rare fireflyまたはmaximum luminanceの不安定化;
- 256-frame結果の再現性不足;
- estimator変更後のbias傾向確認;
- PR reviewで長時間監査が必要と判断された場合。

## 9. Phase 1最終判断とhandoff

Phase 1は診断基盤と標準gateを完成し、filterのvariance低減とsignal-preservation riskを分離できたため完了とする。

Phase 2では独立branchでestimator correctnessを扱う。主な対象はGGX sampling、directional PDF、Cook-Torrance BRDF、`f_r * L_i * (N dot L) / p(L)` throughput、invalid sample、environment miss/geometry hit整合、roughness別収束、mirror limit、低PDF sampleとfireflyである。

Phase 2でestimatorを変更した場合は、Phase 1の同一ROI・paired contractを再利用してvariance、measurement mean、Current-Estimator Mean Baseline RMSEを再評価する。

## 10. English summary

The default-off Surface Variance Filter reduced linear-HDR temporal variance by approximately 75–77% in the two evaluated DamagedHelmet ROIs while changing the 256-frame ROI measurement mean by less than 0.2%. However, it increased per-pixel RMSE to the High-SPP Current-Estimator Mean Baseline. Variance reduction therefore does not justify promotion to a production filter. The baseline is not a physical ground truth, and estimator correctness remains a Phase 2 task.
