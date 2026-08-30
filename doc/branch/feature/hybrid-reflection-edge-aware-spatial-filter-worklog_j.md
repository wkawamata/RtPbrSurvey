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

## 2026-08-30: DamagedHelmet Lit A／B／C／D主観gate

- 細い黄色のEmissive reflectionを確認したgrazing-angle viewでcameraを固定した。全条件でStochastic Rough SamplingとHybrid Reflection contributionを有効に維持した。
- A（`weight 0.0`、spatial off）では時間方向の粒状noiseが明確に見えた。
- B（`weight 0.0`、spatial on）はAと同一に見えた。noise低減、暗化、blur、黄色のにじみはいずれも主観的に識別できなかった。
- C（`weight 0.9`、spatial off）はAより粒状noiseが明確に減り、Emissive reflectionを維持し、問題となる遅延／残像も見られなかった。
- D（`weight 0.9`、spatial on）はCと同一に見えた。追加改善とregressionはいずれも主観的に識別できなかった。
- この限定Lit gateはtemporal history policyの有効性を支持するが、DamagedHelmet上のspatial filterの知覚的価値は示さない。filterはdefault-offを維持し、controlled linear-HDR variance低減は診断証拠としてのみ保持する。

## 2026-08-30: Spatial Filter専用diagnostic scene

- 面内部のsmoothingと境界保持を分離評価するprocedural sceneとして、`Hybrid Reflection Spatial Filter Test`を追加した。
- 4組のCube＋Sphereをdark floorへ部分的にめり込ませた。Cube／floor交差は複数の投影角度を持つ直線edge、Cubeへ半埋没したSphereは曲線状のmaterial／geometry境界を作る。
- pairには同roughnessのmetallic／dielectric差とalbedo差、roughness不連続、near-mirror receiverとrough metallic receiverの隣接を含めた。同roughness条件は、現在のfilterがvisible metallic、albedo、material identityをgateしていない点を意図的に露出する。
- Cube rotationを交互に変え、水平・垂直・斜めの投影edgeを作った。大きなyellow／cyan Emissive targetにより高contrast reflection signalを与え、境界を越えるleakを色で識別しやすくした。
- filter単体比較はStochastic Rough Sampling有効、Temporal History Weight `0.0`、spatial filter off／onを基本とする。Weight `0.9`はtemporal＋spatial相互作用の第2段階確認に使用する。
- scene framing、material識別性、stochastic noise可視性、filter off／on挙動はruntime検証が残る。

## 2026-08-30: Diagnostic sceneから得たspatial効果の解釈

- filterの可視効果は主に同一material surface内部で確認された。時間変化する高輝度Emissive reflection sampleへ空間的なsofteningがかかり、粒子感が低下した。
- material／geometry境界は期待効果の発生源ではない。spatial averageが無関係なsurfaceを越えてradianceを漏らさないことを確認する安全性gateである。
- passはspatialかつstatelessであり、時間蓄積によってtemporal noiseを除去するものではない。位置または強度が時間変化するsampleのframeごとのcontrastと粒子状の見え方を低減する。
- 今後の評価では、同一surface内のgrain低減という主観察と、直線／曲線edge保持という副作用確認を分離する。

## 2026-08-30: 専用sceneの主観評価結果

- Stochastic Rough Sampling有効、Temporal History Weight `0.0`、spatial filter無効では、時間方向の粒状noiseが明確に見えた。
- filterを有効にすると個々の粒子edgeが少し滑らかになったが、全体の粒子感は減らなかった。このため、知覚的に有意なdenoise改善は主張しない。
- Emissive reflectionの平均的な明るさは維持された。
- Cube／floorの直線edge、Sphere／Cubeの曲線edge、metallic／dielectric境界のいずれにも色漏れは確認されなかった。
- 評価条件内でedge rejectionの安全性は支持された一方、固定3x3 kernelは観測した粒子fieldを実質的に抑えるには限定的だった。passはdefault-off／diagnostic-onlyを維持する。
- 複数の大きなEmissive targetが高contrastなreflection sampleを作るため、このsceneではDamagedHelmetよりStochastic Reflectionの挙動を大幅に確認しやすかった。これはproduction代表sceneではなくstress／diagnostic sceneである。

Status: done
