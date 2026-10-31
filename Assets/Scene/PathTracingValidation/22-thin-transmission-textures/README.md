# 薄膜透過テクスチャ

左から、透過率テクスチャのみ／透過色テクスチャのみ／両方。
格子は板の後ろにある。色や透過率が面内で変化することを確認する。

- `rate.png` はRが0、64、128、255の4領域。Rはlinearとしてfactorに乗算する。
- G/B/Aは別の値を入れ、誤って別チャンネルを読んでいないか検証する。
- `tint.png` は4色のRGB。sRGBからlinearへ変換してbaseColorFactorに乗算する。
- textureのAは透過量に使わない。OPAQUE coverageなのでA=0も穴にはしない。
- 透過率0の部分は不透明成分となり、背景の穴ではない。
- 四隅のUVは下辺がv=0となるfixtureなので、画像ファイルと画面の上下は異なり得る。

glTF参照を保存するため、Scene Editor / Saved Scenesから復元可能。
この段階ではScene Editorへのテクスチャファイル選択UIは追加しない。

TEXCOORD_0のみ。texture-info変換や別UV集合、volume、色付き影は未対応。
定数Roughnessによる粗い薄膜透過は後続のscene23で検証する。
粗さテクスチャはscene24で検証する。
既存の共有samplerとmip 0を利用する。色変換は既存baseColorのサンプリング規約に合わせる。
非既定IORと透過率テクスチャの組合せは、混合IOR shadingが未実装のため拒否する。
NRD要求時はシーン全体をNative PTへフォールバックする。

小さなテスト画像とgeometryは `Tests/PathTracing/transmission_texture_fixture.py` で生成する。
画像境界のフィルタ品質や任意glTFの全面対応を、このfixtureだけで合格扱いしない。
