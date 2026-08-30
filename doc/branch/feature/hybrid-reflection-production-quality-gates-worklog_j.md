# Hybrid Reflection Production Quality Gates 作業ログ

## 2026-08-30

- 現在の`main`にあるmerge commit `ac5f6f0`から`features/hybrid-reflection-production-quality-gates`を開始した。
- 現在のdefault値、controlled scene、既存定量report、runtime audit、限定主観結果を監査した。
- production baseline、production candidate、diagnostic experiment、rejected experimentを分離して定義した。
- variance-guided temporalをspatial policy比較へ暗黙に含めない4つの評価profileを固定した。
- 最低限のscene coverageとacceptance gateを記録した。現在のstochastic、temporal、fixed spatial、bounded spatial、variance-guided機能はdiagnostic／default-offのままであり、このPhaseでは昇格していない。
- controlled metallic roughness `0.0`、`0.35`、`1.0`と、確立済みDamagedHelmet noise領域2か所を含む、version管理された名前付きROI profile 5件を追加した。production gate runから手作業の座標転記を除外する。
- `Invoke-ProductionQualityGates.ps1`を追加した。paired sample／temporal sequence、resolved-radiance control variance不変、事前宣言したmean差`0.5%`以内を検証し、variance／tail変化は観測値のまま保持する。
- ad hoc roughness probeで誤った座標を使用し、既知結果と大きく異なる値になったため、このrunはgate根拠から明示的に除外した。名前付き`estimator-metal-r035`の64-frame smokeは既知結果を再現し、sequence一致、temporal variance `+3.92%`、frame-difference p95 `-16.97%`、p99 `-10.03%`、mean差`0.041%`となり、全invariantが合格した。
- strict-mode testでnull array選択bugを検出したため、`ProfileId`未指定時のrunner経路を修正した。明示的なsingle-profile経路には影響していない。
- 名前付き5 profileすべてを64-frame開発levelで実行した。全profileでsample／temporal sequence一致、resolved control維持、事前宣言したmean差`0.5%`以内を確認した。
- controlled roughness `0.0`、`0.35`、`1.0`は既知観測を再現した。DamagedHelmet後頭部寄り面は無変化だった。下面pipeはmean差`0.448%`、temporal variance `+18.57%`、frame-difference mean／p99 `-15.70%`／`-16.24%`だった。
- invariant PASSはproduction品質PASSではない。variance／tailのmixed behavior、後頭部profileでのinactive、以前のLit A/B差なしという結果から、bounded policyはdefault-off diagnosticを維持する。
- 名前付き5 profileすべての256-frame標準gateを完了した。全profileでsequence、resolved control、meanのinvariantに再度合格した。
- controlled roughness `0.0`はmirror bypassを再現した（固定filter比variance `-85.91%`）。roughness `0.35`／`1.0`はvarianceが`6.74%`／`10.70%`増加し、frame-difference p95は`16.61%`／`24.05%`低下した。
- DamagedHelmet後頭部寄り面はreport精度で同一を維持した。下面pipeはmean差`0.426%`、variance `+21.44%`、frame-difference mean／p99 `-15.78%`／`-15.99%`だった。
- 256-frame標準根拠はbounded mean維持を確認する一方、generalなminimum varianceまたは知覚品質改善の主張を棄却する。標準根拠ですでにdiagnostic維持となるため、昇格目的の1024-frame拡張は実施しない。
- 追加glTF asset候補を監査した。BoomBoxはmetallic-roughnessとemissive textureの両方を持ち、Sponzaより固定framingへ収めやすいため選択した。
- 汎用`-AutoSelectGltfAsset <name>`と`-UseSceneDefaults` automationを追加した。既存のDamagedHelmet／Estimator Test専用flagとは相互排他を維持する。
- screenshot automationを要求せず、auto-selected live sceneへArcball framingを適用できるよう`-ReflectionCameraDistanceScale`を拡張した。
- BoomBoxのdefault distanceは品質reviewには広すぎた。version管理defaultsとdistance scale `0.25`により、1920x1080でobject全体を保つreview可能なframingを生成した。64-frame captureは正常終了し、D3D12 error 0件、既知buffer initial-state warning 2件だった。
- BoomBoxのlive Lit A/B主観評価を実施した。固定spatialに対してbounded spatiotemporal spatial policyをONにしても、見た目の差はまったく知覚できなかった。この結果はnon-regressionの限定根拠だが、改善の根拠ではない。
- BoomBoxでも知覚可能な改善を確立できず、256-frame定量結果もmixed behaviorを示しているため、bounded policyはproduction候補へ昇格せずdefault-off diagnosticを維持する。

Status: in progress

