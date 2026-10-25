# PT薄膜透過の粗さテクスチャ

作成日: 2026-10-25
状態: 実装・自動検証完了。ユーザー目視承認は未実施。
前段: [定数Roughnessの薄膜透過](path-tracing-rough-transmission.md)。

## 範囲

非金属の薄膜透過にglTF coreのmetallicRoughnessTextureを許可する。
既存の材質テクスチャindex伝搬を再利用し、GPU Materialのlayoutは変更しない。
粗さは `texture.G * roughnessFactor`。linearデータなのでsRGB変換しない。
metallicFactor=0を必須とするため、画像Bが非ゼロでもmetallicは0になる。
metallicFactorを省略したglTFは既定値1であり、今回の非金属限定条件を満たさない。

- TEXCOORD_0、RGBA8、共有sampler、mip 0の既存契約を維持。
- 無効なtexture/image index、UV欠落、別UV集合、texture-info拡張・変換を拒否。
- 既存のnode/flattened/legacy経路を維持。複数glTF追加時のindex remapを検証。
- RayQueryのmetallic-roughness読み出しにNonUniformResourceIndexを付け、
  同一wave内で異なる材質indexを読む場合を明示。
- 新しいScene Editor画像選択UIは追加しない。保存したglTF asset参照から復元する。
- normal map、metallicとの透過混合、alpha coverage、volume、clearcoat、色付き透過影は対象外。
- NRDは薄膜シーンのNative fallbackを維持する。

TransmissionTestは粗さtexture付きの非金属材質を受理できるようになるが、
同じ原版内のalpha maskやmetallic材質が残るため、原版全体の読み込みはまだ拒否する。

## 保存シーン24

`Assets/Scene/PathTracingValidation/24-roughness-textured-thin-transmission/scene.json`

左はG x 1、中央はG x 0.5、右は定数Roughness 0.5。
Gは0/64/128/255の4領域。R/B/Aはそれぞれ別の値を使う。
同じ板の中で、透過像のぼけ方が変わることを比較する。

## 自動検証

- [x] Debug x64ビルド成功。既存LNK4199警告1件、エラー0。
- [x] CTest 41/41（import受理・拒否、index remap、保存シーン24を含む）。
- [x] Python: 既存透過12件、新規粗さtexture4件、計16/16。
- [x] 実描画6ケース: guide / guide-scaled / guide-zero / guide-varying / furnace / furnace-scaled。
- [x] 既存の不透明・定数透過回帰9キャプチャ。不透明は変更前のexeと完全一致。
- [x] 保存シーン24のキャプチャ。256 samples、1920x1080、Debug Layerエラーなし。
- [ ] ユーザー目視承認。

RAW guideはPathTracing.NormalRoughnessのWを使い、4 ROIで最大絶対誤差0.001を要求する。
空間変化するGはUVの4領域で確認し、factor=1 / 0.5 / 0も確認する。
均一画像のRGBAは `(32,128,255,0)` とし、G以外やsRGB解釈を検出しやすくする。

最終SceneColorは白色炉を使い、実効粗さ `128/255` および `0.5*128/255` を
独立なGGX半球積分と比較する。判定式は前段と同じで実行前に固定する。
generatorと参照の重複を避け、既存の白色炉fixture作成を関数に切り出して再利用する。
実行時状態、入力asset、RAW、ログ、実行ファイル、PT shader、runnerと依存スクリプトのhashを保持する。
ケースの欠落や実行途中のbinary変更は不合格にする。

測定結果:

- guideの最大絶対誤差0.00000765932（許容0.001）。
- guide-scaled: 0.00000382966、guide-zero: 0、guide-varying: 0.00000574449。
- 白色炉の実効粗さ128/255: 参照0.91441795、実測0.91297266、誤差0.00144529。
- 白色炉の実効粗さ64/255: 参照0.99561625、実測0.99536401、誤差0.00025224。
- 白色炉は128 samples、許容値0.01897186。事前に決めた式を変更していない。
- 実描画のD3D12 Debug Layer ERROR / CORRUPTIONなし。

ローカル証拠（Git管理外）:

- `bin/PathTracingValidation/roughness-texture-20261025/report.json`
- `bin/PathTracingValidation/roughness-texture-regression-20261025/report.json`
- `bin/PathTracingValidation/roughness-texture-review-20261025/report.json`
- `bin/PathTracingValidation/roughness-texture-review-20261025/24-roughness-textured-thin-transmission.png`
- 実行ファイルSHA256: `cf4f81e15cf4436bddd7f222585687b09529b443ebadb397be590fcf83f574ae`
- PT shader SHA256: `ad63d4bc27f8204258ab0deb0bb0ce74c6e71f640eebbb920f11afc1a67978f4`

レポート取得後に、fixture generatorのreview専用表示名と古いシーンの制約説明を更新した。
数値用fixtureの材質・画像・形状は変更していない。レポートのhashは取得時点のものを保持する。
前段から残る背景外周の少数画素の再現性差は、今回も解決扱いにしない。

```powershell
python -B Tests/PathTracing/validate_roughness_texture.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/roughness-texture-new-run
python -B Tests/PathTracing/capture_thin_visual_comparison.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --scene Assets/Scene/PathTracingValidation/24-roughness-textured-thin-transmission/scene.json --output bin/PathTracingValidation/roughness-texture-new-review --frames 256
```

出力先は未作成のディレクトリを指定。GPUキャプチャは逐次実行する。

## 参照

- [glTF metallic-roughness material](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#metallic-roughness-material)
