# Hybrid Reflection Multi-Scene Quality Gates 作業ログ

## 2026-08-29: Branch baseline／gate contract

Branch: `features/hybrid-reflection-multi-scene-quality-gates`

Base: `2147cc6` (`Add Hybrid Reflection dynamic temporal diagnostics (#36)`)

このbranchは、個別に検証済みのROIを1つのrepeatableな非主観multi-scene gateへまとめる。保留中のLit perceptual evaluationを置き換えず、renderer quality policyも変更しない。

versioned manifestは次の4 dynamic caseを含む。

- controlled Estimator Testのmetallic sphere、roughness 1.0、0.35、0.0;
- DamagedHelmetのunderside-pipe region。

全caseはlinear-HDR schema 12 diagnostics、stochastic sampling、history weight 0.9、deterministic forward／reverse／stop camera timeline、網羅的temporal-status report、D3D12 Debug Layer logを使用する。sceneごとのorbit magnitude／measurement-window lengthはmanifest dataとして明示する。

aggregate gateは次を要求する。

- process正常終了とcomplete report;
- schema／timeline／rate／settling contract validation;
- D3D12 error／unknown warningなし;
- known warning数が記録済みbaseline以下;
- stationary history acceptance 0.99以上;
- moving outside-history rate 0.10以下;
- controlled caseのvalid T95が30 frames以下;
- DamagedHelmet underside-pipe caseのvalid T95が40 frames以下。

DamagedHelmetのsettling validityはoptionalとする。固定screen-space textured ROIでは停止応答振幅が不足する場合があるためである。このgateはperceptual quality、physical ground truth、object-motion correctness、scene-wide coverage、production denoiser readinessを主張しない。

## 2026-08-29: 実行結果とscope確定

初回suiteでは、ローカルに保存されたinteractive camera overrideによりDamagedHelmet ROIが背景を指した。HDR diagnostics開始時にversioned scene defaultを適用するよう修正し、manifest ROIとcamera timelineをユーザー設定から独立させた。

versioned cameraで再実行した結果:

| Case | Result | Moving acceptance | Stationary acceptance | T50 / T90 / T95 |
|---|---|---:|---:|---:|
| Estimator r1 metallic | PASS | 0.9572 | 1.0000 | 6 / 11 / 13 |
| Estimator r0.35 metallic | PASS | 0.9407 | 1.0000 | 6 / 19 / 20 |
| Estimator r0 metallic | PASS | 0.9494 | 1.0000 | 6 / 15 / 17 |
| DamagedHelmet underside pipes | PASS | 0.9502 | 1.0000 | 7 / 22 / 34 |

全caseでprocess exit 0、D3D12 error 0、unknown warning 0。既知のbuffer initial-state warningはcontrolled caseで3件、DamagedHelmetで2件だった。

旧rearward-surface ROIはversioned default cameraで有効信号を取得できなかった。これを合格扱いにせず、自動manifestから除外した。カメラとROIの再定義、およびLit主観確認は後続の評価branchへ送る。

Debug x64 buildは成功（0 errors、既知のMSBuild import warning 1件）。HLSL custom buildはup-to-date。主観評価はユーザー指示どおり未実施であり、このbranchは非主観multi-scene regression gateのみを完成させる。
