# glTF定数透過: Suzanneの薄膜近似

左から、不透明／無色の薄膜近似／青色の薄膜近似。
後ろの白黒格子が中央・右で見えることを確認する。

## 出典とライセンス

- Model and textures by UX3D GmbH.
- Source: https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0/TransmissionSuzanne
- License: CC-BY 4.0, https://creativecommons.org/licenses/by/4.0/
- Geometry: `TransmissionSuzanne.bin` is copied unchanged (468,480 bytes).
- 本ディレクトリのglTFは改変版であり、原版のレンダリングを再現するものではない。

## 改変内容

- roughnessFactorを0.1から0へ変更し、滑らかな薄膜の対応範囲へ限定。
- 未対応のKHR_lights_punctual、ライトノード、未使用ThicknessMap画像を除去。
- 同一geometryを3つの材質・位置で複製。不透明、無色透過、青色透過とした。
- 青色はbaseColor=[0.2,0.65,1]、通常IOR=1.33を追加。
- 検証用SceneDocument、背景、Native PT presetを追加。

## 対応範囲

定数transmissionFactorとbaseColor tint、滑らかな非金属、通常IORのみ。
透過テクスチャ、粗い透過、volume、未対応の必須拡張は拒否する。
Non-default IORはfactor=1に限定し、未実装の不透明・混合IOR shadingを受理しない。
閉じた形状でも各表面を独立の薄膜として扱う。体積ガラスの屈折・厚み・吸収はない。
Native PT限定。NRD要求時はNative fallbackとなる。Hybrid/Deferredは不透明のまま。
原版はroughness=0.1と必須ライト拡張があるため、そのままの読み込みは未対応。
