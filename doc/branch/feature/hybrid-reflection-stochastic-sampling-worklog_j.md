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

## 2026-08-11: Rough-specular方向の実装

- `HybridReflectionPass`と`ReflectionEvaluatePass`の両方で使用する共通helper `ReflectionSampling.hlsli`を追加した。
- pixel座標とreflection-owned sampling frame indexから、再現可能な等方GGX由来のhalf-vector sampleを1つ生成する。
- camera-to-surface方向をsampled half vectorで反射する。roughnessが`0.001`以下、実験controlが無効、またはsample結果がsurfaceの裏側を向く場合は、正確な完全鏡面方向へfallbackする。
- fallbackを1 sampleに限定し、再sampling loopは追加していない。
- `ReflectionEvaluatePass`で同じsample方向を再現する。これによりpayload resourceを拡張せず、hit surfaceのview-dependent lightingとmiss environment lookupを`RayQuery`がtraceした方向に一致させた。
- stochastic missではenvironment mip zeroを使用する。visible surfaceのlobe拡張は方向分布が担うためである。決定論的pathでは既存のroughness-prefiltered environment lookupを維持する。
- stochastic enable flagと現在のsampling frame index用に、2 DWORDのpixel-shader root constantを追加した。新しいtexture、history、PDF、throughput resourceは追加していない。
- 最初のbuildで、RenderGraph constant keyがpipeline-key structに置かれている誤りを検出した。commit前にconstants-key structへ移動した。
- 検証: 修正後のDebug x64 buildと両HLSL compileはerror 0で成功した。MSBuildには既知のvcpkg重複import warningが残る。デフォルト無効状態でDamagedHelmetを8秒間実行し、D3D12 Debug Layer logが空であることを確認した。生成logは削除した。
- stochastic sampling有効状態のvisual validationは未実施である。

## 2026-08-11: Stochastic capture automation control

- resolved-radiance capture automation用のflagとして`-ReflectionStochasticSampling`を追加した。
- このflagはDamagedHelmetの自動選択後に、既存のデフォルト無効Hybrid Reflection設定を有効化する。interactive defaultおよびproduction defaultは変更しない。
- temporal history weightとsynthetic noiseは独立したCLI controlとして維持する。次のA/B runでは両variantで同じ実stochastic入力を有効にし、history weightだけを変えられる。
- 最初のbuild直前に、別のlocal taskが共有workspaceを自身のbranchへ切り替えていた。pathを3つのCLI編集に限定したstash/switch/popにより、別taskのcommit済みfileと未追跡fileには触れず、目的branchへ変更を復元した。
- 検証: Debug x64 buildはerror 0で成功した。stochastic sampling有効、history weight zeroのresolved-radianceでDamagedHelmetを8秒間実行し、D3D12 Debug Layer logが空であることを確認した。生成logは削除した。

## 2026-08-11: 実信号用subjective suite

- 元のsynthetic-noise modeを維持したまま、既存validation runnerへ`-StochasticSampling`を追加した。
- 専用入力`suite-stochastic.json`と`capture-plan-stochastic.json`を追加した。planは同じwarm-up、移動中、方向反転、停止後収束のtimelineを維持しつつ、suite/case IDと`stochastic-*.png` pathを分離して、以前のcontract captureを上書きしない。
- stochasticの両variantで実rough-reflection samplingを有効にし、synthetic noiseをzeroにする。Aはhistory weight `0.0`、Bは`0.9`を使用する。
- 実ノイズ低減、追従と残像、disocclusion edge、平均輝度、安定detail、停止後収束を対象にした9つのcriterionを維持した。英語と同時に完全な日本語localizationを追加した。
- 静的検証で3つのplan caseがsuite caseおよびA/B pathに正確に対応し、PowerShell runnerにparse errorがないことを確認した。
- 最初の6画像captureは成功し、local evaluatorですべてのPNGが1920 x 1080として読み込まれた。report用の再captureより前にsuiteをcommitし、生成metadataがcleanなsource revisionを示せるようにする。

### 正式な主観評価結果

- commit `5092138`から6画像すべてを再captureし、`workingTreeDirty: false`を確認した。
- capture条件はstochastic sampling有効、synthetic noise `0.0`、Aのhistory weight `0.0`、Bのhistory weight `0.9`である。
- ユーザーはnoise、tracking、brightness、reversal trail、disocclusion/screen edge、settling、stable detailを含む9 criterionすべてを`pass`と判定した。
- defect tagとnotesは記録されていない。保存reportのlocaleは英語だった。
- 個別判定と、反復可能なHTML harness、stable suite、capture planを分離するため、生成reportはlocalのignored artifactとして維持する。
- 判断: sampled-frameの実信号gateは合格した。ただしcapture間のflicker、長時間のestimator bias、live motion品質はまだ測定していないため、この結果だけではproduction history defaultを非ゼロにする根拠にはしない。

## 2026-08-11: Live motion A/B結果

- commit済みの`capture-plan-stochastic-live.json` timelineを追加して実行した。初期静止、2回の方向反転を含む3つの低速orbit区間、初期yawへの復帰、最後のsettling holdで構成する。
- Aはstochastic sampling、history weight `0.0`を使用した。ユーザーは静止中にも不安定な領域があり、移動中のedgeと内部reflectionが不安定で、方向反転後も不安定さが続き、停止後にも安定画像へ収束しないと観測した。
- Bは同一timelineでhistory weight `0.9`を使用した。ユーザーはgrain/flickerが減少し、edgeと内部reflectionが全体として安定し、方向反転時の残像や遅れを感じず、古いreflectionの残留なしに停止後も問題なく安定したと観測した。
- Bでは移動中のedgeに軽微なflickerが残った。平均輝度とdetailには明らかな不自然な損失がないと判定した。
- 判断: history weight `0.9`は現在の1 sample stochastic入力に明確なlive安定化効果を持ち、軽微な移動edge問題を残しながらDamagedHelmet live-motion gateに合格した。
- global production defaultはstochastic sampling無効、history weight `0.0`のまま維持する。将来の明示的なstochastic-temporal presetを支持する証拠にはなるが、1 scene、1つの非ゼロweight、現在の近似estimatorだけではglobal defaultを暗黙に変更するには不十分である。
- Spatial denoiseとscene coverage拡大は別のfollow-upとし、残るedge flickerへの反応としてこのbranchへ追加しない。

## 2026-08-11: Phase closeout

- 規範的なHybrid Reflection contractへ、実装済みのdirection-sampling境界、sample-index ownership、決定論的fallback、Evaluateでの方向再現、条件付きmiss environment lookup、近似上の制約を反映した。
- foundation noteはcurrent statusと歴史的なroot constant説明が古くなった箇所だけを更新した。詳細な規範semanticsはfocused contractに維持する。
- raw payloadとevaluated/resolved radiance layoutが不変であり、PDF、throughput、denoise、confidence、追加history resourceを導入していないことを確認した。
- 最終defaultはstochastic sampling無効、temporal history weight `0.0`、temporal debug noise `0.0`を維持する。
- closeout時の検証は、成功したDebug x64 buildとHLSL compile、空のD3D12 Debug Layer run、clean commitから生成した6画像suiteの9 passおよびdefectなし、上記live A/B結果で構成する。
- 残る移動edge flicker、scene/roughness coverage拡大、別のhistory weight、長時間mean bias測定、spatial denoiseは、このbranchの未完了作業ではなく明示的なfollow-up候補とする。
