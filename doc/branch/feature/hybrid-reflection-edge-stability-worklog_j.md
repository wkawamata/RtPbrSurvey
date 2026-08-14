# Hybrid Reflection Edge Stability 作業ログ

英語版: [Hybrid Reflection Edge Stability Work Log](hybrid-reflection-edge-stability-worklog.md)。

## 2026-08-12: 診断用フレーミング

- 撮影専用オプション `-ReflectionCameraDistanceScale` を追加した。初期 Arcball 距離へ一度だけ倍率を適用し、capture plan の timeline 全体で同じ距離を維持する。
- DamagedHelmet の edge-stability 撮影では倍率 `0.5` を使う。interactive camera と通常撮影の default を変えず、従来のおよそ縦横2倍の見かけサイズにする。
- variant `edgezoom` で stochastic plan の3地点を撮影し、移動中、方向反転、停止後のすべてでヘルメットが画面内に収まることを確認した。
- Debug x64 build は error 0件で成功した。自動実行では D3D12 error はなく、既存の committed buffer initial-state warning 2件のみだった。
- 生成した PNG と runtime log は local validation artifact とし、commit しない。

## 2026-08-12: Temporal Validity 分類

- 新しい render target を増やさず `Temporal Validity` debug view を追加した。`ReflectionResolvedRadiance.a` は診断 metadata を保持し、RGB は未加重 resolved-radiance contract と従来の blend 動作を維持する。
- 分類色は黒=historyなし、青=history画面外、赤=depth reject、黄=normal reject、緑=history採用とした。
- 既存 capture-plan automation で分類表示を再現できるよう `-CaptureReflectionTemporalValidity` を追加した。
- 拡大した DamagedHelmet の移動中と方向反転 checkpoint では、normal reject が細い幾何 edge と内部形状の edge に集中した。depth reject は silhouette/disocclusion の一部だけに現れた。
- 停止後 checkpoint では画像全体が history採用へ戻った。恒常的なhistory不正よりも、移動中の正当なrejectによってnoisyなcurrent sampleが露出することが、観測されたedge flickerの主因候補である。
- この証拠だけでdepth/normal thresholdを緩めない。次の比較では正当なrejectを維持し、disoccluded historyを残さずreject画素を安定化できるかを調べる。
- Debug x64 build はerror 0件で成功した。自動実行ではD3D12 errorはなく、同じ既存committed-buffer initial-state warning 2件のみだった。

## 2026-08-13: Reject画素近傍補助の実験

- depthまたはnormal history reject後の画素だけに、current-frame radianceの3x3平均を適用するdefault-off実験を追加した。
- 近傍採用条件には既存のvisible-depth許容値`0.002`とvisible-normal dot閾値`0.9`を使う。history採用画素、history閾値、disocclusion判定は変更しない。
- 実験設定を変更するとreflection historyをresetする。人工noiseも平均前に近傍sampleごとへ独立に注入する。
- stochastic sampling有効、history weight `0.9`、拡大DamagedHelmetでA/B checkpointを撮影した。Aは実験無効、Bは実験有効である。
- 静止画の予備確認ではBの移動frameで粒状性が減り、停止後のA/Bはほぼ同じだった。一方Bは移動中の細いhighlightもわずかに平滑化するため、自動合格とはせず主観評価を必要とする。
- 安定性、detail維持、境界漏れ、disocclusion、停止後、輝度を確認する3case・9基準の`suite-edge-stability.json`を追加した。local evaluatorで英語・日本語表示を確認した。
- Debug x64 buildはerror 0件で成功した。両方の自動実行でD3D12 errorはなく、同じ既存committed-buffer initial-state warning 2件のみだった。

## 2026-08-14: Mobile主観評価レポート

- mobile評価用にedge-stability evaluatorと対象を限定した6枚のA/B画像をGitHub Pagesで公開した。reportはuser download artifactのままとしcommitしない。
- 2026-08-14評価の`hybrid-reflection-edge-stability-v1` report version 1を受領した。
- 移動中は安定性改善、detail維持、境界維持の3/3がpassだった。
- 方向反転は安定性改善、detail維持、disocclusion安全性の3/3がpassだった。
- 停止後の見え方、detail残存、輝度はすべてunable to judgeで、pass 0、fail 0、unable 3だった。
- defect flagとnoteは記録されていない。
- 判断: 実験はdefault-offを維持する。移動中の狙った改善と、移動境界で報告されたregressionがないことは確認できたが、停止後の副作用gateは未完了である。
- 次は停止後だけの小さな再評価またはlive停止観察を行う。このgateを解決する前にpolicyを拡大したりglobal defaultを変更したりしない。

## 2026-08-14: 停止後gateの限定再評価

- camera移動がframe 180で停止した後の1、6、15 framesを撮る、縮小した停止後専用capture planを追加した。
- detail維持、reflection/輝度維持、収束、持続artifactだけを判定する3case・6基準の英日suiteを追加した。
- 同じstochastic、history `0.9`、拡大camera条件で、Aはreject画素近傍補助無効、Bは有効として撮影した。
- 予備確認ではframe 1でnoise低減とわずかな細部平滑化が見え、frame 6で差がかなり小さくなり、frame 15でほぼ同等になった。
- 未完了のmobile停止後gate用として、新画像とsuiteを既存GitHub Pages evaluatorへ公開する。
- この単位ではcode変更がないため、直前の成功済みDebug x64 buildを有効とし再実行しなかった。

## 2026-08-14: 停止後gate結果

- `hybrid-reflection-edge-settling-v1` report version 1を受領した。
- 停止1 frame後は2基準ともunable to judgeだった。noteにはA/B双方で上面から背面materialにnoiseが残ると記録された。
- 停止6 frames後はdetailとreflection内容の2基準がfailだった。
- 停止15 frames後はA/B同等性と持続artifactの2基準がfailで、noteはframe 6と同じ観測を参照している。
- 集計はpass 0、fail 4、unable 2だった。構造化されたdefect checkboxは選択されていないが、noteは両variantに共通する上面・背面materialの持続noiseを示している。
- user補足: 観測されたnoiseは主にedge artifactではなく、特定materialの面全体を覆っている。このbranchが対象とするmoving-edge rejection問題とは分離し、material/surface全体のstochastic varianceとして分類する。
- 位置補足: 対象は滑らかな頭頂panelそのものではなく、それに隣接する後頭部寄りの面である。
- asset確認: DamagedHelmetのglTF materialは`Material_MR`の1つだけで、material index 0を使うprimitiveも1つである。したがって対象は別material IDではなく、同じmaterial内でmetallic/roughness textureやnormal mapの値によって区別される領域である。
- 判断: reject画素近傍補助実験をproductionまたはglobal-default policyとして採用しない。先の移動中6/6 passは局所的な効果の証拠として保持するが、停止後gateはfailし、共通の持続noiseも未解決である。
- review可能性のため実装はこのbranchでdefault-offのまま保持する。このphaseではrejection thresholdを弱めたりfilterを拡大したりしない。
- これで計画した最初のpolicy比較を完了する。追加作業はこのbranchへ別の推測的edge policyを重ねず、頭頂panelに隣接する後頭部寄りtexture領域のstochastic varianceを調べる独立scopeへ分割すべきである。

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
