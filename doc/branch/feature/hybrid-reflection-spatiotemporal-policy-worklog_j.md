# Hybrid Reflection Spatiotemporal Policy 作業ログ

## 2026-08-30

- merge済みedge-aware spatial-filterを基点として、`features/hybrid-reflection-spatiotemporal-policy`を開始した。
- 既存metadata境界を監査した。`ReflectionSpecularConfidence`は持続的な高varianceの根拠であり、history validityまたは正しさの確率ではない。`ReflectionSpecularMoments`はweighted estimatorのtemporal luminance momentsを保持する。
- temporal history ownershipは変更していない。spatial outputはstatelessのままで、reflection historyへ帰還しない。
- 永続GPU resourceを追加せず、`Spatial Policy Inputs` debug viewを追加した。現在のspatial neighborhood gateを再計算し、R=confidence、G=mapped temporal variance、B=採用されたcenter以外のneighbor比率を表示する。
- このviewはobservability専用である。filterが適用されたこと、採用neighborがunbiasedであること、estimatorが物理的に正しいことは主張しない。
- Debug x64とHLSL compileは成功した。build diagnosticは既知のvcpkg duplicate-import warningのみだった。

Status: in progress
