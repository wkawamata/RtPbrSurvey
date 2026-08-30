# Hybrid Reflection Spatiotemporal Policy 作業ログ

## 2026-08-30

- merge済みedge-aware spatial-filterを基点として、`features/hybrid-reflection-spatiotemporal-policy`を開始した。
- 既存metadata境界を監査した。`ReflectionSpecularConfidence`は持続的な高varianceの根拠であり、history validityまたは正しさの確率ではない。`ReflectionSpecularMoments`はweighted estimatorのtemporal luminance momentsを保持する。
- temporal history ownershipは変更していない。spatial outputはstatelessのままで、reflection historyへ帰還しない。
- 永続GPU resourceを追加せず、`Spatial Policy Inputs` debug viewを追加した。現在のspatial neighborhood gateを再計算し、R=confidence、G=mapped temporal variance、B=採用されたcenter以外のneighbor比率を表示する。
- このviewはobservability専用である。filterが適用されたこと、採用neighborがunbiasedであること、estimatorが物理的に正しいことは主張しない。
- Debug x64とHLSL compileは成功した。build diagnosticは既知のvcpkg duplicate-import warningのみだった。
- 固定filter比較経路を維持したまま、独立したdefault-off `Spatiotemporal Spatial Policy` toggleを追加した。
- spatial passと同時に有効化した場合、policy strengthはpersistent confidence、relative standard deviation、visible roughnessのgateを乗算し、blendを`0.75`以下に制限する。既存のdepth、normal、roughness、hit/miss、hit distance、hit normal rejectionが常に優先される。
- policyはstatelessかつpost-temporalのままである。toggle変更でhistoryを無効化せず、spatial outputをhistoryとして使用しない。
- HDR diagnostic report schema version 14へpolicy stateを追加した。
- policy変更後にDebug x64を再buildし、C++と影響する全HLSLが成功した。warningは既知のvcpkg duplicate-importのみだった。
- default-off DamagedHelmet runtime smokeはexit code 0でcaptureに成功し、D3D12 errorは0件だった。既知のbuffer initial-state warning 2件だけが再現し、unknown warningはなかった。
- 再現可能なautomation用に`-ReflectionSpatiotemporalSpatialPolicy`を追加した。このflagはspatial passを暗黙に有効化しないため、paired runでは`-ReflectionSurfaceVarianceFilter`を同一に保ち、policy flagだけを変えられる。
- 最初の64-frame paired runではpolicyが完全bypassした。confidence更新が別consumerである`Variance-Guided Temporal` toggle内に誤って閉じていたためconfidenceが0のままだった。一方momentsはvarianceを報告しており、ROIのvarianceが0ではなくevidence production欠落が原因と確認した。
- confidence生成とtemporal weight消費を分離した。accepted historyでは全modeでconfidence metadataを更新し、`Variance-Guided Temporal`はそのevidenceをtemporal history weightへ使うかだけを制御する。同toggleがOFFならtemporal RGBは変わらない。
- 分離後にEstimator scene ROIのpaired測定を再実行した。64-frameではsample/temporal indexが一致し、固定filter比でbounded policyのtemporal varianceは3.92%高い一方、frame difference p95は16.96%、p99は10.03%低く、ROI mean差は0.041%だった。
- 256-frame標準gateでも方向を再現した。sample/temporal indexは一致し、temporal varianceは6.74%高く、frame difference mean/p95/p99は14.50%/16.61%/10.31%低く、ROI mean差は0.026%だった。
- この結果はbounded-tail policyとして解釈できるが、最小varianceは主張しない。対象はcontrolled 48x48 ROI 1件であり、multi-scene generalizationまたは物理的正しさは確立しない。
- paired reportをschema version 2へ更新し、明示的なvariant labelとBのA比signed variance changeを追加した。互換性のため既存filter-off/on fieldは維持する。
- 既知のmetallic roughness 0.0、0.35、1.0 ROIへ64-frame controlled gateを拡張した。全pairでsampling/temporal frame indexが一致し、resolved-radiance controlも完全一致した。
- roughness 0.0ではconfidenceが0で、bounded policyはresolved controlへbypassした。固定filterはROIのtemporal varianceを約8.4倍へ増加させ、policyはその固定経路比でvarianceを88.08%低減した。ROI mean差は0.013%、frame difference p95は同一だった。
- roughness 1.0ではpolicyのtemporal varianceは固定filterより7.62%高いが、frame difference mean/p95/p99を19.43%/24.04%/0.47%低減した。ROI mean差は0.018%だった。
- roughness 0.35の結果と合わせ、stable mirror evidenceではbypassし、roughかつhigh-confidenceな領域では長期meanを維持しながらspatial mixingを制限する意図をcontrolled gateが支持した。Lit主観品質と追加scene coverageは未確認である。

Status: in progress
