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

Status: in progress

