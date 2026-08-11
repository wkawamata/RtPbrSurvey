# Hybrid Reflection Stochastic Sampling 作業ログ

英語版: [Hybrid Reflection Stochastic Sampling Work Log](hybrid-reflection-stochastic-sampling-worklog.md)。

このログは、Hybrid Reflectionの確率的サンプリングに関する設計判断、実装単位、検証結果を記録する。リソースと時間方向処理の規範的な意味は[Hybrid Reflection Contracts](hybrid-reflection-contracts.md)に置く。

## 2026-08-10: フェーズ開始と現行信号の監査

- `main`の`87b6f56`から`features/hybrid-reflection-stochastic-sampling`を開始した。
- 現在の反射レイ方向が、決定論的な完全鏡面方向`reflect(viewDirection, normal)`であることを確認した。
- visible surfaceのroughnessは現在`maxRoughness`によるレイ生成可否にだけ使われ、レイ方向の分布を広げていないことを確認した。
- hit surfaceのroughnessは`ReflectionRayMaterial`内の独立したmaterial payloadであり、visible surfaceのsampling lobeを制御してはならないことを確認した。
- `HybridReflectionPass`にframe/sample indexや乱数seedの入力がないことを確認した。
- 最初の実装境界を固定した。確率的方向生成は`HybridReflectionPass`内の`RayQuery`前に置き、raw hit/material payloadとevaluated/resolved radiance contractは変更しない。
- stochastic samplingとtemporal挙動が検証を通過するまでは、production defaultで現在の決定論的画像を維持する。
- Temporal accumulation、spatial denoise、DLSS RR/Streamline integration、PathTracing、大規模なRenderGraph変更はこのフェーズの対象外とする。
- 検証ゲートとして、history weight `0.0`と`0.9`で決定論的入力と確率的入力を比較し、時間方向のノイズ低減、平均輝度、detail保持、motion trail、方向反転、停止後の収束を確認する。
- この単位ではrendererとshaderの挙動を変更していないため、buildは実行していない。

## 計画した小さな実装単位

1. reflection-owned sample index/reset contractを定義し、最小のrough-specular sampling modelを選ぶ。
2. resource semanticsを変えずに、デフォルト無効のCPU/shader controlを追加する。
3. visible surfaceの確率的rough-specular方向を実装し、compile/buildで検証する。
4. 実際の確率的信号を測定するために必要な範囲だけ、既存の反復可能なA/B suiteを拡張する。
5. 観測した効果がproduction defaultを非ゼロにする根拠になるか確認する。自動的には有効化しない。

## 2026-08-10: Sampling modelとownershipの決定

- `ReflectionEvaluatePass`はtrace方向から到来するradianceを評価し、`LightPass`は引き続きvisible surfaceのdistance、roughness、intensity、Fresnel weightingを所有する。
- 現在の最終合成は、明示的なPDFとthroughput項を持つMonte Carlo BRDF estimatorではなく、意図的なreflection approximationである。方向を確率化するだけの実装を、unbiased path tracingや物理的に完全なGGX積分とは表現しない。
- 最初の実験モデルには、等方GGX由来のrough-specular方向を使用する。visible surfaceのroughnessがsampling lobeを制御し、hit surfaceのroughnessはhit materialのradiance評価だけに使用する。
- stochastic samplingが無効な場合、またはvisible roughnessが実質的にゼロの場合は、完全鏡面方向を正確に維持する。
- visible surfaceの裏側を向くsample方向はrejectする。最初の実装では、無制限な再sampling loopを導入せず、回数が限定された決定論的fallbackを選ぶ。
- 各sampleはpixel座標とreflection-owned sample indexからseedする。capture automationで再現可能であり、swap-chain back-buffer indexに依存しないこと。
- Hybrid Reflection信号を実行した場合、submit後にsampling indexを進める。history invalidationではsampling indexもresetし、最初のstochastic sampleとtemporal historyを同時に再開する。
- raw payload、evaluated radiance、resolved radianceのresource layoutは変更しない。このフェーズではpayloadにPDFやthroughput fieldを追加しない。
- stochastic samplingはデフォルト無効の実験controlとして公開する。有効状態または強度を変えた場合はreflection historyをinvalidateする。
- 平均輝度の維持は、初期近似で保証された性質ではなく、測定するacceptance gateとして扱う。

## 2026-08-11: デフォルト無効controlとsample indexの配線

- レイ方向はまだ変更せず、デフォルト無効の`Stochastic Rough Sampling` Debug UI設定を追加した。
- enable flagとreflection-owned sampling frame indexについて、対応するCPU pass constantとHLSL constantを追加した。
- CPU/HLSLでfield順を一致させ、Hybrid Reflectionのroot constantsを9 DWORDから11 DWORDへ拡張した。
- sampling commit-pending flagを追加した。`HybridReflectionPass`がadvance待ちを記録し、そのpassを含むdirect-queue command listのsubmit後にだけindexを進める。
- reflection historyと同時にsampling indexをresetする。stochastic enable stateの変更は両方をinvalidateし、空のhistoryとsample zeroから実験を開始する。
- sampling ownershipをswap-chain indexおよびTemporal Upscaler frame indexから独立させた。
- この単位には確率的方向計算がまだないため、controlを有効にしても決定論的な完全鏡面レイを維持する。
- 検証: Debug x64 buildとHLSL compileはerror 0で成功した。MSBuildは既知のvcpkg重複import warningを報告した。app-local deploymentは利用できない`pwsh.exe`からWindows PowerShellへfallbackして完了した。
