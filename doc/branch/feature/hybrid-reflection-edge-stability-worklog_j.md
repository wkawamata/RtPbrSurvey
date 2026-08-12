# Hybrid Reflection Edge Stability 作業ログ

英語版: [Hybrid Reflection Edge Stability Work Log](hybrid-reflection-edge-stability-worklog.md)。

このログは、stochastic Hybrid Reflectionで残る移動edge flickerの診断、制御された検証、限定的な安定化作業を記録する。信号とhistoryの規範的semanticsは[Hybrid Reflection Contracts](hybrid-reflection-contracts.md)に維持する。

## 2026-08-12: フェーズ開始

- `main`の`6f552d8`から`features/hybrid-reflection-edge-stability`を開始した。
- 前フェーズはhistory weight `0.9`で実信号6画像suiteとDamagedHelmet live A/B gateに合格したが、ユーザーは移動中のedgeに軽微なflickerを観測した。
- このフェーズでもglobal defaultはstochastic sampling無効、temporal history weight `0.0`を維持する。
- 修正を選ぶ前に原因を診断する。すべてのedge不安定性をdenoise問題として扱わない。

## 診断順序

1. rough metal、glossy dielectric、小さく明るいreflection、silhouette/disocclusionの制御された条件で問題を再現する。
2. 同一camera timelineでhistory weight `0.5`、`0.75`、`0.9`を比較する。
3. 次の原因候補を分離する。
   - stochasticなhit/miss切り替え
   - depthまたはnormal history rejectionの切り替わり
   - nearest samplingされたresolved radiance
   - visible roughnessの不連続
   - historyをrejectすべき通常のdisocclusion
4. 既存viewで原因を区別できない場合は、rejection/filtering policyを変更する前に観測専用instrumentationを追加する。
5. 最初の実装単位では測定に基づく最小の安定化policyを1つだけ選び、sampled-frameとlive A/B validationを繰り返す。

## 初期scope

対象:

- 既存scene infrastructureを使った、反復可能な複数条件validation sceneまたはcamera plan
- 必要な場合のaccepted/rejected historyまたはreflection hit/miss用の軽量debug分類
- radianceだけのbilinear history sampling、roughness-aware history weight、threshold policy、reflection固有rejection、neighborhood clampなどから、測定された限定的変更を1つ
- 同じcommit内での英語版・日本語版worklog更新

対象外:

- 汎用spatial denoise pass
- DLSS Ray ReconstructionまたはStreamline integration
- PathTracing
- 大規模RenderGraphまたはdescriptor architecture変更
- より広い証拠なしでのglobal stochastic/temporal default変更

## 判断gate

- 不安定性が正当なdisocclusion rejectionなら、edgeを時間方向に滑らかに見せるだけのためにrejectionを弱めない。
- history対応が有効でnearest radiance samplingがstepを発生させる場合は、depth/normal validity testを離散的に維持したまま、radianceだけのbilinear samplingを試す。
- stochastic hit/miss切り替えが支配的なら、spatial filteringより前にreflection固有evidenceを評価する。
- 複数caseで単一原因が支配的でなければ、複数の推測的fixを組み合わせず、診断で止めてfollow-upを分割する。
- 最初の測定フェーズ終了時に、policyを追加する前に計画規模を再評価する。
