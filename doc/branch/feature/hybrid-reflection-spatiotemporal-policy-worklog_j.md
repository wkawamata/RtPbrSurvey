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

Status: in progress
