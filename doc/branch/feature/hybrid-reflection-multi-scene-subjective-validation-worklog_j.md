# Hybrid Reflection Multi-Scene Subjective Validation 作業ログ

## 2026-08-29: Controlled Estimator live Lit評価

Branch: `features/hybrid-reflection-multi-scene-subjective-validation`

Base: `c881cb4` (`Add Hybrid Reflection multi-scene quality gates (#37)`)

実アプリの`Hybrid Reflection Estimator Test`をLit表示で評価した。A/B共通条件はHybrid Reflection contribution ON、stochastic rough sampling ON、history weight `0.9`。Aはvariance-guided temporal／persistent confidence OFF、BはONとした。

ユーザー評価:

- 静止時の粒状noise: Bで少し減った可能性があるが、差は弱く断定しない。
- 移動時の遅れ／残像: Bで増加しない。
- 方向反転: 破綻しない。
- 停止後: 安定して収束する。
- 輝度／detail: 不自然な損失は見られない。

判定: Bはこのcontrolled live testで動的またはcomposition regressionを示さなかった。静止noiseについては弱い改善傾向として記録するが、明確な知覚改善またはproduction readinessは主張しない。

次はDamagedHelmetを実アプリで評価し、下面pipeと後頭部寄りの既知noise領域を別々に確認する。

## 2026-08-29: glTF asset path blocker

Computer Useから実行したアプリでDamagedHelmetをUI loadすると、`GltfObjectViewerScene::Load()`の`assert(loaded)`が発生した。原因はscene descriptorの相対path `Assets\\...`がprocess current directory基準で解決され、repository外のworking directoryから起動するとassetを発見できないことだった。

glTF loaderに限定したpath resolverを追加した。absolute pathまたはcurrent directoryで有効な既存pathは維持し、それ以外のrelative pathは実行ファイルdirectoryを基準に再解決する。これにより通常のrepository-root起動を変えず、desktop launcher／Computer Use／異なるworking directoryからの起動を安定化する。

検証:

- Debug x64 build: 成功、0 errors、既知のMSBuild import warning 1件。
- working directoryを`C:\\Windows`へ固定し、`-AutoSelectGltfDamagedHelmet`で起動: exit code 0。
- 20 warm-up frames後のPNG capture: 成功。
- D3D12 Debug Layer: error 0、既知のcommitted-buffer initial-state warning 2件。

この検証により、repository外working directoryからDamagedHelmetをloadできることを確認した。主観A/B評価は未完了のまま保持し、ユーザー帰宅後に再開する。
