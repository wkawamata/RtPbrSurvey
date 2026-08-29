# Hybrid Reflection Edge-Aware Spatial Filter 作業ログ

## 2026-08-29: 実装境界監査

- `main` の `91220dd` から `features/hybrid-reflection-edge-aware-spatial-filter` を開始した。
- 既存default-off `Surface Variance Filter` は `TemporalReflectionPass` 内で動作し、temporal accumulation前の `ReflectionEvaluatedRadiance` をvisible depth、normal、roughness、metallicだけでneighbor gateしていることを確認した。
- 既存実験は新denoiser contractのspatial境界ではない。独立outputを持たず、resolved radianceを消費せず、reflection hit class／hit distanceも使用しない。
- 最初の実装sliceではtemporal resolve後にstateless `EdgeAwareSpatialReflectionPass` と独立 `ReflectionDenoisedRadiance` resourceを追加する。無効時は現在のpass graphとLightPass inputを維持する。
- spatial outputはtemporal historyへfeedbackしない。これによりfilter off／on observabilityとPR #40で固定したtemporal-history ownershipを維持する。

## 2026-08-29: 独立spatial pass実装

- transient `ReflectionDenoisedRadiance`、stable SRV／RTV binding、`TemporalReflectionPass` 後の独立 `EdgeAwareSpatialReflectionPass` を追加した。
- 既存default-off設定を旧temporal前filter branchから新temporal後passへ移した。無効frameではpassを追加せず、`LightPass` は `ReflectionResolvedRadiance` を直接bindする。
- stateless 3x3 kernelはvisible depth、visible world normal、visible roughness、hit／miss class、hit distance、decode済みhit normalの互換性を要求する。near-mirror pixelはfilterをbypassする。
- spatial output alphaは `1` とし、Temporal Validity metadataは `ReflectionResolvedRadiance.a` に維持する。spatial outputはtemporal historyへfeedbackせず、pass toggleもhistoryをinvalidateしない。
- Debug x64と影響HLSLはerror 0件、既知のvcpkg重複import warning 1件で成功した。
- controlled sceneのmatched Lit smoke captureをfilter off／onで実行し、両方exit code 0、D3D12 error 0件、既知committed-buffer-state warning各3件だった。on captureではrough sphereのgrainがboundedに低下し、明白なsilhouette leakは見られなかった。定量／主観quality gateは未実施である。

## 2026-08-29: Linear-HDR paired開発gate

- HDR diagnostic captureに独立report対象 `ReflectionDenoisedRadiance` signalを追加した。filter無効時は明示的identity fallbackとして `ReflectionResolvedRadiance` を読み、temporal signalは対照として別途reportする。
- paired runnerがtest sceneを明示選択し、変化しないtemporal outputをfilter結果と誤認せずspatial outputを比較するよう更新した。
- roughness `0.35` metallicのcontrolled ROIを64 frame測定し、sample／temporal index列が一致することを確認した。spatial outputはtemporal varianceを `29.2514%` 低減し、mean luminance差は `0.1488%` だった。
- off／onの `ReflectionResolvedRadiance` 対照varianceはreport精度で同一だった。測定した静止条件では、新spatial passがtemporal historyへfeedbackせず影響も与えないことを確認した。
- これはcurrent estimatorに対する開発level gateである。物理的正しさ、production denoiser readiness、motion品質、scene横断generalizationは主張しない。256-frame PR gateとlive主観評価は今後の作業として残す。

## 2026-08-30: DamagedHelmetのgrazing-angle Emissive Reflection観察

- Emissive material付近のヘルメット側面を浅い角度から見たとき、細い黄色のEmissive reflectionを確認した。直接見えているEmissive sourceと反射像は、表示面とviewing geometryから区別した。
- Stochastic Rough Sampling有効、Temporal History Weight `0.0`では、Lit reflection上に時間方向の粒状noiseが明確に見えた。
- Temporal History Weightを`0.9`へ上げると、Emissive reflectionを維持したまま主観的に見えるtemporal noiseが消えた。
- これによりDamagedHelmet上に再現可能なLit観察点が成立した。Weight `0.0`はspatial filter単体効果の分離、Weight `0.9`はtemporal安定化後にspatial passが有用な改善を加えるかdetailを失うだけかの確認に使用できる。
- motion応答、filter off／onのdetail保持、scene横断generalizationはまだ主張しない。

Status: in progress
