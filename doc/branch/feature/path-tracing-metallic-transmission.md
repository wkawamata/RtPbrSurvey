# PT金属度と薄膜透過の混合

作成日: 2026-10-25
状態: 実装・自動検証完了。ユーザー目視承認は未実施。
前段の透過率／透過色／粗さテクスチャ対応はcommit `4d51d72`。

## 混合モデル

既存の不透明BRDFと滑らかな／粗い薄膜BSDFを再利用する。
実効金属度はMR texture.B（linear）とmetallicFactorの積。
`T = transmissionFactor * transmissionTexture.R`、`M = metallicFactor * MR.B` とする。

```text
B = (1-T) * B_opaque(M)
  + T*(1-M) * B_thin_dielectric
  + T*M * B_metal
```

- 確率1-Tで既存の不透明材質、T*(1-M)で薄膜、T*Mで完全金属を選択する。
- 薄膜成分はFresnel反射と透過を含む。金属成分は透過しない。
- 不透明成分に元の中間metallicを残す。単に透過率を(1-M)倍するだけでは
  残りの不透明成分でmetallicを二重適用するため、その方式は使わない。
- T=0は以前の不透明BRDFそのまま。M=0は以前の薄膜混合そのまま。
- M=1ではTを無視し、不透明金属の経路を使う。T=0またはM=1では透過用乱数を消費しない。
- 成分の選択確率がその成分のBSDF係数なので、throughputへ同じ係数を再度掛けない。
- NEEとBSDFは選択した成分で対を作り、MISでは条件付きPDFを使用する。
  薄膜ローブ自身のNEEは前段から未実装で、環境・発光面はBSDF tracingで評価する。

glTFの金属は透過しないという要件を満たす端点を追加する。
ただし既存opaque BRDFを保持した混合モデルであり、全中間値に対してglTF BRDF全体との
厳密な一致を主張しない。opaqueのSchlickと薄膜の厳密Fresnel、多重散乱energy loss等の
前段の近似・制限を引き継ぐ。

## 裏面と実行境界

薄膜シーンではFORCE_NON_OPAQUEを維持するが、裏面candidateの受理はテクセルの
`T*(1-M)>0` で判断する。完全金属や実効透過率0の裏面は既存opaqueと同様にカリングする。
中間透過材質は薄膜の両面を扱う既存方針を引き継ぐ。
glTF doubleSidedの全面実装や、混合成分ごとの別個のsidednessは今回の範囲外。
NRDは静的な薄膜シーン判定により、完全金属テクセルだけの場合も保守的にNative fallbackとする。

## glTF

- roughnessFactorとmetallicFactorはfinite、0..1。
- core既定metallicFactor=1を受理し、その値を保持する。
- MR textureのG/Bと各factor、既存のindex remap・UV/image検証を利用する。
- 実シーン材質でMR textureが未指定なら白の中立fallbackを使う。
  以前のsemantic fallbackはB=0で、定数metallicFactorを消していた。
  生成された既定の材質群のfallback設定は今回変更しない。
- alpha MASK/BLEND、normal map、volume、clearcoat、glTFライトは引き続き別段階。
- TransmissionTest全体はalpha mask材質があるためまだロード不可。
- StainedGlassLampはalpha mask、volume、clearcoat等が残る。
- Suzanne原版は必須glTFライトが残る。

## 検証

- [x] Debug x64ビルド成功。初回は既存LNK4199警告1件、最終buildは警告0・エラー0。
- [x] CTest 42/42。金属度7種類 x 透過率4種類 x 1024 samplesのGPU契約テストを追加。
- [x] 新GPU契約テストはWARP / hardwareで合格。D3D12 Debug Layerエラーなし。
- [x] Python: 透過関連17件、粗さtexture4件、計21/21。
- [x] 実描画8ケース: dielectric / mixed / metal / opaque-control / textured-mixed /
  textured-dielectric / guide / metal-backface。
- [x] 既存の定数透過9キャプチャ、粗さtexture6ケースの回帰。
- [x] 保存シーン25のキャプチャ。256 samples、1920x1080、Debug Layerエラーなし。
- [ ] ユーザー目視承認。

初回の実描画ではmixedがdielectricと同じ値になり、不合格となった。
MR未指定時のB=0 fallbackが原因だった。初回failedレポートは
`bin/PathTracingValidation/metallic-transmission-20261025/report.json` に保持する。
実シーン材質のfallbackを白へ修正し、判定値は変えずに別フォルダで再実行する。
この修正により、glTF等の不透明な定数metallic材質も正しいfactorが反映される。
既存の「metallicが消えていた」見た目まで保存する変更ではない。

第2回は金属反射の寄与が約9%不足した。低roughnessのopaque GGXで分布の分母を
1e-6へクランプし、実際のNDF sampling分布よりPDFを小さくしていたため、
環境MISのBSDF寄与が減衰する問題と切り分けた。
`(1-cos)*(1+cos) + alpha^2*cos^2` の安定形へ直し、物理的なピークをクランプしない。
既存のalpha下限0.001は維持する。法線方向のDとspecular PDFをdouble精度基準と照合する
GPUテストを追加した。r2のfailed証拠も保持し、同じ環境モード5・同じ許容値で再実行する。
この修正は共有opaque GGXに作用するため、既存の定数透過・粗さtexture回帰も実施する。
不透明材質のNRD基準画像にも影響する可能性がある。NRD契約CTestは合格したが、
NRD全体の画像品質suiteや主観評価を今回再実施したとは扱わない。

第3回は同じ環境モード5、128 samples、同じ許容値で8/8合格した。

| ケース | 実測RGB平均 |
|---|---|
| dielectric | 0.80775581 / 0.61573962 / 0.42385496 |
| mixed (M=0.5) | 0.90366407 / 0.80754301 / 0.71141634 |
| metal (M=1) | 0.99960492 / 0.99960492 / 0.99960492 |
| opaque-control | metalと中央ROIの全画素完全一致 |
| textured-mixed | 0.90405649 / 0.80833272 / 0.71259892 |
| textured-dielectric | dielectricと同じ平均 |
| metal-backface | 0.79980469 / 0.59960938 / 0.39990234（背景） |

MR texture Bとfactorのguideも合格。全8ケースでD3D12 Debug Layerエラーなし。
ケースを省略せず、失敗時の許容値を緩めずに修正と再実行を行った。
前段の背景外周の少数画素の再現性差は、今回も解決したとは扱わない。

ローカル証拠（Git管理外）:

- 初回: `bin/PathTracingValidation/metallic-transmission-20261025/report.json`
- 第2回: `bin/PathTracingValidation/metallic-transmission-r2-20261025/report.json`
- 第3回: `bin/PathTracingValidation/metallic-transmission-r3-20261025/report.json`
- 定数透過回帰: `bin/PathTracingValidation/metallic-thin-regression-20261025/report.json`
- 粗さtexture回帰: `bin/PathTracingValidation/metallic-roughness-regression-20261025/report.json`
- 保存シーン: `bin/PathTracingValidation/metallic-transmission-review-20261025/report.json`
- 比較画像: `bin/PathTracingValidation/metallic-transmission-review-20261025/25-metallic-thin-transmission.png`
- 第3回exe SHA256: `a424844bc5a242307fedf2b940dc9ecc6bc0c8422d204606ce324f50181e4ed0`
- 第3回PT shader SHA256: `e9caf0abbc03d38220705dffb1f9f9b542ce0ba12095480d65cb5a76dc1cb669`

実描画は白色反射環境、既知のUnlit背景 `[0.8,0.6,0.4]`、白色材質、roughness=0、T=1。
CPU側の独立Fresnel計算から `M + (1-M)*(F + (1-F)*background)` を予測する。
判定予算は事前固定の `0.003 + 3.3/sqrt(ROI画素数 * frames)`。
中央ROIでのweight幅1.1以下と独立サンプルを仮定した6-sigmaの平均誤差予算に、
量子化とほぼdeltaな金属のGGX alpha下限の誤差を加えた限定基準である。
任意の粗さ・斜入射・照明への品質保証ではない。
完全金属と裏面背景は固定0.001。M=1のT=0/1は中央ROIの全画素完全一致も要求する。
不透明controlはtransmission/IOR拡張自体を持たないcore glTF材質とする。
MR textureのBはMaterialF0MetallicのWを4 ROIで読み、最大絶対誤差0.001を要求する。
実行時状態、case一覧、画像・入力・ログ・binary・shader・scriptのhashを保存する。

保存シーン: `Assets/Scene/PathTracingValidation/25-metallic-thin-transmission/scene.json`。
左からM=0 / 0.5 / 1、T=1、roughness=0.25。金属度による格子の透過の減衰を比較する。

```powershell
python -B Tests/PathTracing/validate_metallic_transmission.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/metallic-new-run
python -B Tests/PathTracing/capture_thin_visual_comparison.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --scene Assets/Scene/PathTracingValidation/25-metallic-thin-transmission/scene.json --output bin/PathTracingValidation/metallic-new-review --frames 256
```

出力先は未作成のディレクトリを使う。GPUキャプチャは逐次実行する。

## 参照

- [glTF transmission: Transparent Metals](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission#transparent-metals)
