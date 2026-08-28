# Hybrid Reflection Dynamic Temporal Diagnostics レポート

## 1. 状態

Status: **PASS WITH LIMITATION / Lit主観評価保留**

このPhaseは、Hybrid Reflectionのcamera motion、方向反転、停止後のtemporal behaviorをlinear-HDR domainで説明可能にする診断Phaseである。object回転時の主観的な追従遅れを確認済み不具合とは仮定せず、先にcontrolled camera motionを数値化した。

## 2. 実装した診断contract

HDR diagnostic report schema 12は次を記録する。

- automation frame index、forward／reverse／stationary phase、camera yaw offset;
- stored GBuffer motion vectorのROI mean／maximum NDC magnitude;
- hit rateとhit pixel限定のmean hit distance;
- no history、outside history、depth reject、normal reject、history acceptedの個別rate;
- Evaluated／Resolved Radianceを含む既存linear-HDR statistics;
- 停止後のResolved Radiance ROI mean luminanceに対するT50／T90／T95。

T50／T90／T95は、停止直後のerrorに対する残差が50%／10%／5%以下の状態を3 sample連続で満たす最初のframe offsetとする。終端8 stationary samplesの平均をsettled valueとする。停止直後の変化量がsettled meanの1%以下の場合は、意味のある正規化対象がないためmetricをinvalidとする。

## 3. Controlled measurement

Scene: `Hybrid Reflection Estimator Test`

共通条件:

- 30度camera orbit;
- forward 12 frames、reverse 12 frames、以後stationary;
- stochastic rough sampling有効;
- temporal history weight `0.9`;
- 32 warm-up frames;
- visual validation済み48x48 metallic sphere ROI。

| ROI | Moving motion mean (NDC) | Moving acceptance | Depth reject | Normal reject | Stationary acceptance | T50 | T90 | T95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| roughness 1.0 | 0.003949 | 0.9572 | 0.0424 | 0.0004 | 1.0000 | 6 | 11 | 13 |
| roughness 0.35 | 0.013917 | 0.9407 | 0.0590 | 0.0003 | 1.0000 | 6 | 19 | 20 |
| roughness 0.0 | 0.014832 | 0.9494 | 0.0439 | 0.0067 | 1.0000 | 6 | 15 | 17 |

motion magnitude差はscreen position／projected motion差を含むため、roughness単独の効果とは解釈しない。全caseで停止後はmotion 0、history acceptance 1.0へ戻った。測定されたT90は11～19 frames、T95は13～20 framesだった。

weight 0.9の途切れないscalar EMAでは、旧historyが10%残るまで約22 frames、5%まで約29 framesである。今回の結果がそれより速いのは、reprojection、history rejection、空間的に変化するsignalを含むためである。このcamera-motion条件では、過度に長いhistory tailは検出されなかった。

## 4. Artifact validation

`Tests/HybridReflection/Test-DynamicTemporalReport.ps1`は次を独立再検証する。

- schema／timeline一致;
- automation frame連続性;
- temporal status rateの総和;
- moving中のnonzero motionとstationary中のzero motion;
- settling validityとT50／T90／T95。

schema番号更新前binaryで生成したartifactを実際にrejectし、schema 12で再生成した3 reportはすべてPASSした。Debug x64／HLSL buildはerror 0件。各GPU runのD3D12 Debug Layerはerror 0件で、既知のbuffer initial-state warning 3件のみだった。

## 5. 主張範囲

主張できること:

- controlled camera motionのreprojection／rejection／settlingを再現可能なreportから説明できる。
- 評価した3 ROIでは、停止後20 frames以内にT95へ到達した。
- 通常描画default、temporal policy、filter defaultに変更はない。

まだ主張しないこと:

- object motion contractが正しいこと;
- reflection hit identity変化を完全にrejectできること;
- Lit表示で知覚的artifactがないこと;
- DamagedHelmetまたは他sceneへの一般化;
- production denoiser readiness。

## 6. 残gate

ユーザーがworkstationへ戻った後、controlled sceneのlive Lit表示で移動、方向反転、停止後追従を確認する。このgateがPASSならcamera-motion診断を閉じる。実用的artifactが見つかった場合だけ、object-motion／hit-identity診断を次の優先作業へ昇格する。
