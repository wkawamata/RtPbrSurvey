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

