# PT薄膜透過テクスチャ

作成日: 2026-10-24
ブランチ: `codex/pt-transmission-textures`
ベース: `origin/main` の `3f80bb4`（薄膜透過PR #103マージ済み）。
状態: 対象範囲の実装・自動検証完了。シーン22のユーザー目視承認は未実施。

本メモはテクスチャ追加時点の範囲・証拠を記録する。
後続のRoughness factor対応は [粗い薄膜透過](path-tracing-rough-transmission.md) に分けて記録する。

## 今回の範囲

glTFの `transmissionTexture.R` をlinearの透過率としてfactorに乗算する。
`baseColorTexture.RGB` は既存baseColor経路と同じsRGB変換を行い、透過色factorに掛ける。
テクスチャのalphaを透過率に転用しない。定数材質と不透明材質の動作を維持する。

- CPU/glTF材質からGPUへ透過率用・透過色用の2つのtexture indexを伝搬。
- テクスチャなしは白のfallback。SceneDocumentの独立した透過色とbaseColorを混同しない。
- GPU Materialは120 bytes。旧フィールドoffsetは維持し、新indexを112/116へ追加。
- glTF追加時のtexture index remap、TEXCOORD_0、RGBA8、indexと型の検証。
- Scene22に透過率のみ／色のみ／併用のGit管理fixtureを置く。
- 実描画のRAW SceneColorを使い、linear R、sRGB色、factor乗算、UV、alpha非使用を確認する。

## 対象外

粗い透過、metallic透過、volume、clearcoat等との複合、alpha MASK/BLEND、色付き影、体積屈折。
別UV集合やtexture-info変換は拒否する。既存の共有samplerとmip 0を利用する。
新しいScene Editorの画像選択UIは今回追加しない。glTFパスを保持する保存・復元を使う。
NRD要求時は引き続きNative fallbackとする。
非既定IOR＋透過率テクスチャは、非透過領域のIOR shadingが未実装なので拒否する。
色変換とフィルタは既存baseColorの規約に合わせ、境界でのフィルタ品質まで今回の数値合格としない。

TransmissionTest原版にはrough/metal/alpha coverageもあるため、今回だけで全体を受理しない。
StainedGlassLamp原版のvolumeや複合材質も未対応のまま維持する。

## 検証

実行前に固定する数値判定:

- 4領域のテクセル境界から離れたROIを使う。透視投影とFresnelはCPU側で独立に計算する。
- 透過色だけのIOR=1・factor=1は決定的な比較とし、各画素のRGB最大絶対誤差0.001。
- 確率的な透過率はROI平均を使い、許容値を各色で
  `0.001 + 6 * amplitude / (2 * sqrt(ROI画素数 * samples))` とする。
  これはBernoulli分散の上限1/4と独立な画素サンプルを仮定した保守的な予算であり、
  任意シーンの収束やフィルタ境界の品質保証ではない。実測結果を見て許容値を変更しない。
- 既定64 samples。color／rate／scaled-rate／combined／shared-image／nrd-fallbackの6ケース。
- NRD要求によるNative fallbackはcombinedと同一出力を要求する。
- レポートは画像・ログ・source asset・実行ファイル・runnerのhashと実行時状態を保持する。

途中の不合格と切り分け（判定値は維持）:

- 初回は検証用glTFの背景z符号が逆で板の手前に来ていた。左右手系変換後に背景が奥となる
  配置へ修正し、配置の回帰テストを追加した。初回failedレポートは保持する。
- r2では透過の5ケースが合格したが、NRD切替の全画面一致は不合格。
  透過面から離れた背景外周6画素に差があり、Nativeを同条件で再撮影しても背景5画素に差が出た。
  色の差は背景色が一部サンプルに混ざる挙動と整合するが、原因は未確定。
  追加前から存在した問題かも未確認。NRDの不具合・GPUの不具合とは断定しない。
- 当初fixtureの全画面一致は未合格のまま残す。以降の定量fixtureは背景面とmissの色を同じ
  `[0.8,0.6,0.4]` に揃え、透過式を測る制御背景とする。RGB判定値は変更しない。
  制御背景fixtureの合格を、元の背景再現性問題の修正や任意シーンの完全一致と混同しない。
- 元の証拠: `bin/PathTracingValidation/transmission-textures-r2-20261024/`。

- [x] Debug x64全体ビルド、関連CTest。
- [x] glTFのtexture index伝搬・remap・不正入力の拒否。
- [x] RAW実描画: RGBのsRGB変換、alpha非使用、Rのみのlinear透過率。
- [x] factor乗算、併用、同一画像を色／透過率へ別解釈、NRD fallback。
- [x] 定数透過の既存結合テストと不透明出力の回帰。
- [x] シーン22のキャプチャ。
- [ ] シーン22のユーザー目視承認。編集UI操作の受入確認は自動検証に含めない。

### 実行結果

- Debug x64ビルド成功。最終incremental buildは警告0・エラー0。
  NRD / Streamline SDKありの構成を使用。今回SDKなし構成の再ビルドは未実施。
- CTest: 40/40合格。Python透過関連テスト: 9/9合格。
  sandbox内では一時フォルダ保存が拒否されたため、通常ユーザー権限で再実行して合格。
- 制御背景RAW: 6ケース、各4 ROI、64 samplesで合格。
  colorの最大ROI平均誤差は0.0000976563。
  確率ケースの最大ROI平均誤差は0.00198481で、事前に定めた各ROIの統計予算内。
  combinedとNRD fallbackのRAWは全画面完全一致。
- 既存定数透過: 9キャプチャで合格。旧112-byte Materialの実行ファイルとの不透明出力は
  全画面完全一致。透過色の最大誤差0.000292969、2枚重ね0.0000390625。
- シーン22: 1920x1080、128 samples、maxBounces=8でPNG取得成功。
  Debug LayerのERROR / CORRUPTIONなし。これは目視承認の代わりではない。

ローカル証拠（生成物のためGit管理外）:

- `bin/PathTracingValidation/transmission-textures-r3-20261024/report.json`
- `bin/PathTracingValidation/transmission-textures-regression-20261024/report.json`
- `bin/PathTracingValidation/transmission-textures-review-20261024/report.json`
- `bin/PathTracingValidation/transmission-textures-review-20261024/22-thin-transmission-textures.png`
- 未解決の背景比較: `bin/PathTracingValidation/transmission-textures-r2-20261024/background-control-report.json`

検証実行ファイルSHA256:
`49f906b8d581fac68f8c687354fe28a2f1ba314f2066d46861abadcdbee8accb`

PathTracing CSMain（生成先と配布先の一致を確認）SHA256:
`e30ff1b5540b623c167dc3f2d7f31297738ef725e26088a99c589a31a88065a2`

### 再実行

出力先は未作成のディレクトリを指定する。GPU検証は同時実行しない。

```powershell
ctest --test-dir bin/PathTracingValidation/thin-gltf-build-20261024 -C Debug --output-on-failure
python -B -m unittest discover -s Tests/PathTracing -p 'test_*transmission*.py'
python -B Tests/PathTracing/validate_transmission_textures.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/transmission-textures-new-run
python -B Tests/PathTracing/capture_thin_visual_comparison.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --scene Assets/Scene/PathTracingValidation/22-thin-transmission-textures/scene.json --output bin/PathTracingValidation/transmission-textures-new-review --frames 128
```

Python runnerにはPillowとNumPyが必要。旧実行ファイルとの回帰は
`validate_thin_transmission.py --opaque-reference-exe <変更前exe>` を使用する。
旧exeは対応する旧shader一式と一緒に保持する。

## 参照

- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [glTF 2.0 material specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#materials)
