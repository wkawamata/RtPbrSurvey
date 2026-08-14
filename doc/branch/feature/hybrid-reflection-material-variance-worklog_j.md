# Hybrid Reflection Material Variance 作業ログ

英語版: [Hybrid Reflection Material Variance Work Log](hybrid-reflection-material-variance-worklog.md)。

このログはDamagedHelmetの特定texture領域に残る、面全体のstochastic variance診断を記録する。reflection signalとhistory semanticsは[Hybrid Reflection Contracts](hybrid-reflection-contracts.md)に維持する。

## 2026-08-14: Phase開始

- edge-stability phase完了後、`main`のmerge commit `a749d80`から`features/hybrid-reflection-material-variance`を開始した。
- 再現領域は、滑らかな頭頂panelに隣接する後頭部寄りsurfaceと、下面に露出するpipeである。
- DamagedHelmetは1つのprimitiveに1つのglTF material `Material_MR`を持つ。対象は別material IDではなくtextureで定義される領域として扱う。
- 症状はedgeだけでなく面の一部全体を覆い、camera停止後も見える。
- global defaultはstochastic sampling無効、temporal history weight `0.0`を維持する。reject画素近傍補助実験もdefault-offを維持する。

## 初期診断scope

1. visible roughness、metallic、normal、evaluated radiance、resolved radiance、temporal validityを同じviewで撮影する。
2. 2つの再現領域がroughness、metallic、normal-map周波数、hit/miss挙動、高輝度environment samplingのどれを共有するか確認する。
3. 持続するinput varianceとhistory rejectionを分離する。temporal validityがgreenでも、1 sample/frameの収束が遅ければvarianceは残り得る。
4. sampling/filtering policyを変更する前に、観測専用instrumentationまたはcapture metadataを優先する。
5. 1つの原因が支持された場合だけ、後続の測定単位でdefault-off policyを最大1つ試す。

## 対象外

- 広範なspatial denoise
- depthまたはnormal rejection thresholdの緩和
- DLSS Ray ReconstructionまたはStreamline integration
- PathTracing
- global default変更
- 前回実験への別edge-stability policyの積み重ね

## 判断gate

- varianceが高roughnessまたはtexture roughnessに対応する場合、material identityではなくsampling varianceを調べる。
- varianceがnormal-map周波数に対応する場合、radianceをfilterする前にdirection stabilityを確認する。
- historyが継続して採用されてもnoiseが残る場合、rejection変更前に収束とsample varianceを測る。
- 2領域で原因が共通しない場合、1つのfixへ押し込まず分割する。
