# features/hybrid-reflection-hdr-variance-diagnostics

## 目的

estimator変更またはfilter昇格の前に、Hybrid Reflectionのvarianceと収束をlinear-HDR signal domainで測定する。このphaseでは既存のdefault-off Surface Variance Filterを、同じ現在の近似estimatorに対してpaired条件で比較する。

このphaseで主張できるのは、policyが現在のestimatorに対してvarianceを下げ、測定した許容範囲内で長期meanを維持することだけである。物理的正しさ、unbiasedness、energy conservation、production denoiser readiness、physical referenceへの収束は確立しない。

## 測定レベル

| Frames | 用途 |
| ---: | --- |
| 64 | 開発中の高速gate |
| 256 | 標準PR評価 |
| 1024 | mean drift、rare firefly、bias傾向の任意拡張監査 |

1024-frame levelはすべての変更で必須にしない。256-frame結果または観測した外れ値が追加costを正当化する場合だけ実行する。

## 必須metric

固定ROIとsignal boundaryごとに以下を記録する。

- linear-HDR luminanceのtemporal mean;
- temporal varianceとstandard deviation;
- coefficient of variation `sigma / mean`。near-zero mean時のpolicyを明記する;
- frame間luminance絶対差のmean、p95、p99;
- High-SPP Current-Estimator Mean Baselineに対するRMSE;
- filter on/offの長期mean差;
- ray hit率;
- temporal acceptance率;
- depth reject率;
- normal reject率;
- 最大luminance。単独quality metricではなくfirefly用の補助指標とする。

## Paired comparison contract

- A/Bを同じsample indexへresetする。
- camera、animation状態、history reset frameを固定する。
- 検証対象filter以外の全settingを一致させる。
- A/Bで同じframe範囲を測定する。
- warm-up frameとmeasurement frameを分離する。
- version付きの置換を明示記録しない限り、確認済み1920x1080 DamagedHelmet ROIを維持する。
  - `rearward_surface`: x `895`, y `278`, width `75`, height `85`;
  - `underside_pipes`: x `805`, y `585`, width `125`, height `135`。

## 初期実装監査

- `ReflectionEvaluatedRadiance`と`ReflectionResolvedRadiance`はrender-sizeのpersistent `DXGI_FORMAT_R16G16B16A16_FLOAT` textureである。既存PNG captureは表示出力を変換するため、linear-HDR測定経路ではない。
- 既存`DebugDumpCapture`にはfull-texture copy/readbackとhalf-float decodeの実例があるが、LightPass/back-buffer検証専用である。Phase 1は既存dump contractを変更せず、そのcopy/readback patternを再利用する。
- `ReflectionRayHit`は`R16G16B16A16_FLOAT`である。`.x`はcommitted hit distance、`.y`はhit flag、`.zw`はoctahedral hit normalである。新規signal bufferなしでhit率とhit-distance統計を取得できる。
- `ReflectionResolvedRadiance.a`は現在diagnostic temporal statusを持つ。`0.0`はhistoryなし、`0.25`はout of bounds、`0.5`はdepth reject、`0.75`はnormal reject、`1.0`はacceptedである。Phase 1は既存diagnostic metadataを利用できるが、RGB radiance semanticsを変更せず、alphaをproduction confidence contractとして扱わない。
- 最初の実装sliceはevaluated radiance、resolved radiance、ray hit、temporal statusの固定ROI texelだけをcaptureする。新しいfull-frame variance textureまたはdenoise passを追加しない。

## 計画gate

1. version付き64-frame linear-HDR ROI captureとJSON resultを1つ実装する。
2. paired sample/history resetとA/Bの同一frame windowを証明する。
3. 必須metricとHigh-SPP Current-Estimator Mean Baseline terminologyを追加する。
4. 同じ経路を256-frame標準gateへ昇格する。
5. mean drift、rare firefly、bias傾向が未解決の場合だけ1024 framesを実行する。

## 実装ログ

### 2026-08-15: ROI HDR readback primitive

- `DXGI_FORMAT_R16G16B16A16_FLOAT`のreflection signal専用readback helperを追加した。
- helperは要求ROIを検証し、その範囲だけをcopyしてlinear-HDR RGBA sampleをdecode可能にする。
- tone mappingとPNG変換は意図的にこの経路へ含めない。
- このsliceではRenderGraph capture schedulingと統計出力はまだ接続しない。次のPhase 1 integration boundaryとする。
- HLSL custom build stepを含むDebug x64 buildは成功した。既存のvcpkg重複import warningだけが報告された。

### 2026-08-15: RenderGraph capture integration

- reflection HDR診断用に、明示的な1-frame engine request/result境界を追加した。
- scene rendering後にdiagnostic RenderGraph passを追加した。同じsubmit frameのevaluated radiance、現在のresolved-radiance write slot、ray-hit payloadをtransitionしてcopyする。
- Deferred renderingとHybrid Reflection contributionが有効でない場合はrequestを拒否し、未生成signalのreadを防止する。
- readbackはoffline診断用として意図的に同期方式を維持する。連続capture schedulingとJSON集計を次の境界とする。
- Debug x64 buildは成功し、既存のvcpkg重複import warningだけが報告された。

### 2026-08-15: 連続frame schedulingとJSON smoke

- `-ReflectionHdrDiagnostics <report.json>`を追加し、warm-up、frame count、render-space ROIをoverride可能にした。
- diagnostic modeはDamagedHelmet、Deferred rendering、Hybrid Reflection contributionを明示的に選択する。screenshot capture automationとは同時利用不可とした。
- schemaにはlinear-HDR domain、referenceなし、render解像度、ROI、frameごとのevaluated/resolved mean luminance、hit率、temporal acceptance率、depth reject率、normal reject率を記録する。
- 32 warm-up frames後、1920x1080の`rearward_surface`で64-frame runが完走し、reportに64 framesすべてが記録された。
- 64-frame runのD3D12 errorは0件で、既知のcommitted-buffer initial-state warning 2件だけを記録した。
- これはcollection continuityだけを検証する。temporal variance、distribution percentile、paired A/B比較、baseline RMSEはまだ主張しない。
