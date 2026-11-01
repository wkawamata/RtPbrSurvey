# PT粗い薄膜透過

作成日: 2026-10-24
状態: Roughness factorの実装・自動検証完了。ユーザー目視承認は未実施。
透過テクスチャ対応と同じ作業ブランチ上の次段階。
後続の [粗さテクスチャ対応](path-tracing-roughness-textures.md) は別記する。

## 順序

1. Roughness factorによるGGX薄膜透過（今回）。
2. metallic-roughness textureのGによる粗さ。
3. metallicとの混合とalpha coverage。
4. glTFライト、clearcoat等の複合材質。
5. volume、厚み、体積屈折・吸収、色付き透過光。

各段階で対応範囲を確認し、原版3モデルの全面対応とは区別する。
既存のScene EditorのRoughnessをそのまま使い、新しい材質パラメータは追加しない。

## モデル

glTFの薄膜BTDFに従い、GGXの反射方向を巨視的な法線面で反転して透過方向とする。
巨視的な屈折や体積は追加しない。粗さ0は以前のdelta反射／直進透過を維持する。
GGX可視法線は既存のHeitzサンプラーを再利用する。

- `alphaR = max(roughness^2, 0.001)`。
- 透過ローブはKhronos Sample RendererのIOR補正に合わせ、
  `alphaT = max(roughness^2 * clamp(2*ior-2,0,1), 0.001)`。
- IOR=1は反射もrough blurもないdelta透過。
- 巨視的法線は既存の滑らかな薄膜経路と同じgeometry normalを使用する。
- マクロFresnelでreflection/transmissionを選択。ローブのFresnelはhalf vectorで評価する。
- 条件付きVNDF PDFと選択確率を掛け、weightは `f * abs(NdotL) / pdf`。
- 微小面の反射が巨視的表面の裏に落ちるサンプルは無寄与として棄却。再抽選しない。
- 単一散乱GGXのため、高粗さでの多重散乱energy compensationは未対応。
- 薄膜ローブはBSDF samplingのみ。NEEを実施していないので、emissive/environmentの
  hit時に競合しないNEEのMIS重みを掛けない。deltaの真偽とは別のフラグで管理する。
  点・平行・spot等のdelta lightを薄膜ローブで直接サンプルするNEEは今回対象外。
- NRDは以前と同じくシーン全体をNative PTへfallback。

## glTF境界

非金属、OPAQUE coverage、constant roughness factorを今回受理する。
粗さtexture・normal texture・volume・clearcoatはまだ拒否する。
非既定IORのopaque/mixed shadingは未実装のため、既存のfull transmission限定を維持。
Suzanne原版のroughness=0.1は受理できるが、必須のKHR_lights_punctualはまだ別の拒否理由。

## 検証

- [x] Debug x64ビルド成功。初回警告0。最終buildは既存のLNK4199が1件、エラー0。
- [x] CTest 41/41。新GPUテストは28,672 samplesをdouble精度の独立計算と照合。
- [x] 方向、PDF、BSDF、weight、below-surface棄却、斜入射、IOR=1、粗さ0の極限。
- [x] glTF roughness factorの受理・保持、範囲外とmetallicの拒否。
- [x] 既存の定数透過・不透明回帰: 9キャプチャ、不透明は旧exeと完全一致。
- [x] 既存の透過率／透過色texture回帰: 6ケース合格。
- [x] 白色炉: 滑らか／粗さ0.5／粗さ1.0／環境BSDFモードの4ケース合格。
- [x] Scene23の実描画比較: 256 samples、1920x1080を取得し、ぼけを確認。
- [ ] ユーザーの目視承認。

旧 `validate_thin_transmission.py` はroughness=0.5を設定していたが、以前は薄膜経路で
無視していた。今回から粗さが意味を持つため、滑らかな極限の回帰はroughness=0を明示する。
旧保存シーン17-20の非ゼロroughnessも今後は実際に反映される（元データを書き換えない）。

### 白色炉の判定

白色の無限環境、白色の薄膜、IOR=1.5、中央ROIで比較する。
法線入射の単一散乱GGXを出射半球上で独立に数値積分する。
roughness=1の解析解 `1-ln(2)` との一致もPythonで検査する。
統計的許容値は実行前に `0.006 + 4.2/sqrt(ROI画素数 * frames)` と固定する。
中央ROIでのほぼ法線入射と量子化に0.006、weight幅1.4以下・独立サンプルを仮定した
6-sigmaの平均誤差予算を残りに割り当てる。任意の斜入射・材質に使う基準ではない。
滑らかな場合は固定0.001。結果を見て許容値を変更していない。

128 samplesの結果:

| Roughness | 参照積分 | 実測RGB平均 | 絶対誤差 |
|---|---:|---:|---:|
| 0 | 1 | 1 | 0 |
| 0.5 | 0.91581243 | 0.91441127 | 0.00140116 |
| 1 | 0.30685282 | 0.30516535 | 0.00168747 |

環境NEE-only設定でも薄膜ローブ自身はBSDF-onlyであり、未使用のNEEとのMISで暗くしてはいけない。
環境モード2と5の粗さ0.5中央ROIは完全一致した。

ローカル証拠（Git管理外）:

- `bin/PathTracingValidation/rough-thin-smooth-regression-20261024/report.json`
- `bin/PathTracingValidation/rough-thin-textures-regression-20261024/report.json`
- `bin/PathTracingValidation/rough-thin-furnace-20261024/report.json`
- 最終のshader hash・実行状態検証版: `bin/PathTracingValidation/rough-thin-furnace-r2-20261024/report.json`
- `bin/PathTracingValidation/rough-thin-review-20261024/report.json`
- `bin/PathTracingValidation/rough-thin-review-20261024/23-rough-thin-transmission.png`

GPU契約テストはWARPおよびhardwareで合格、D3D12 Debug Layerエラーなし。
Python透過関連テスト12/12合格。
前段で見つかった背景外周の少数画素の再現性差は、今回も解決したとは扱わない。

最終実行ファイルSHA256:
`6f73376d5756419d2a8398fe178e552772fdae10cc33ec8ee31d30316a259113`

最終PathTracing CSMain SHA256:
`1843494dcfa1e5fd552c8147606f30ed23b1c122f678b79e2307ed65821fdc71`

```powershell
python -B Tests/PathTracing/validate_rough_transmission.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/rough-thin-new-run
python -B Tests/PathTracing/capture_thin_visual_comparison.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --scene Assets/Scene/PathTracingValidation/23-rough-thin-transmission/scene.json --output bin/PathTracingValidation/rough-thin-new-review --frames 256
```

出力先は未作成のディレクトリを使う。GPUキャプチャは逐次実行する。

## 参照

- [glTF薄膜BTDF仕様](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [Khronos Sample Renderer punctual transmission](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/punctual.glsl)
- 可視法線samplerは既存 `PathTracingDenoiserSampling.hlsli`（Heitz 2018、2019修正）を再利用。
