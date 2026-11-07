# Diffuse IBL動的遮蔽のglTF MASK対応課題

## 背景と現状

- Diffuse IBL Visibilityは画素ごとに半球方向へRayQueryを実行し、環境へ到達した割合を動的に求める。
- 現状の遮蔽レイは `RAY_FLAG_FORCE_OPAQUE` を使い、glTF `alphaMode: MASK` の透明部分を判定しない。
- 通常のSponzaにはMASKマテリアルが3種類、対象プリミティブが14個ある。テクスチャには葉・花・鎖が含まれる。
- 既存の検証画像はMASKプリミティブを除外したSponzaによるもので、通常のSponzaのMASK対応を示すものではない。
- 診断用Force ONではMASKを不透明扱いして計算する。透明部分も遮蔽するため、結果は正式なMASK対応ではない。設定は既定OFFで、シーン設定に保存・復元する。
- transmission / volumeはForce ONの対象外であり、別課題とする。

## 必要な対応

1. MASK三角形をnon-opaque候補として判定できるようにし、不透明ジオメトリは既存の高速経路を維持する。
2. 候補ヒットのinstance、mesh、primitive、materialを正しく解決し、重心座標からUVを補間する。複数mesh、instance、materialを含むケースにも対応する。
3. glTFのBase Color alphaと`baseColorFactor.a`、`alphaCutoff`を使い、透明部分を通過し不透明部分だけ遮蔽物として確定する。UVとテクスチャの取り扱いは既存描画経路との整合性を確認する。
4. 両面マテリアルと表裏の扱いを確認する。テクスチャLODやフィルタ方法が細い葉や鎖の判定に与える影響も評価する。
5. `Shaders/SceneRayQuery.hlsli`とPathTracingの既存MASK候補処理の再利用を検討し、可視性パスに必要なデータ・descriptor・resource state・lifetimeを設計する。
6. MASKに対応できた場合だけ通常のVisibilityを有効にする。未対応条件とForce ONの警告は誤解なく表示する。

## 検証条件

- 合成テスト: 透明領域は光を通し、不透明領域は遮る。alpha cutoff前後、base color alpha factor、両面、UV境界を検証する。
- 複数mesh / material / instance、変換、カメラ移動、動的遮蔽物を検証する。
- 通常Sponzaで植物や鎖を保持したままON/OFFとPTを比較する。Force OFF / ON / 正式MASK対応を区別する。
- 不透明のみの既存シーンで結果と性能の回帰を確認する。Sponzaではray数別のGPU時間、ノイズ、ちらつきも測定する。
- 遮蔽maskだけでなくDiffuse成分と最終画像を見る。PTと比較する場合はカメラ、環境、露出、寄与成分を明記する。
- D3D12 Debug Layer、standaloneと外部ホストのbuild/test、実画面確認を行う。

## 非目標と関連課題

- transmission / volumeの光透過は本課題に含めない。
- 遮蔽率は間接反射やカラー・ブリーディングを生成しない。PTとの差のすべてがMASKによるとは限らない。
- Specular環境光の遮蔽とHybrid Reflectionの合成は `sponza-specular-visibility-followup.md` を参照する。
- ノーマルマップによる自己交差、bias、画面内フィルタの適切性は独立して検証する。
