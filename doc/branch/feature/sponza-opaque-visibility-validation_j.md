# MASKを除外したSponzaでの環境可視率評価

## 検証対象

元の`Assets/Models/Sponza/glTF/Sponza.gltf`は変更しない。
MASKを使う14 primitivesと3 materialsを検証用コピーから除外し、残る89 primitives・
22 opaque materialsを保持する。材質をOPAQUEへ書き換えて穴を塞ぐ方法は使わない。

material/mesh indexを再配置し、空になったmeshを持つnodeではmesh参照だけを外す。
nodeのtransform/childrenは維持する。buffer/imageは元のresourceへ相対参照する。
元アセットのSHA256を記録し、検証後に変更されていないことを確認する。

これはgeometryを一部除いたsceneであり、元のSponza全体の合格判定ではない。
MASKに対応したRayQueryは別incrementとする。

## 条件と再現

```powershell
python Tests/PathTracing/validate_sponza_opaque_visibility.py --output bin/PathTracingValidation/sponza-opaque-new
python -m unittest discover -s Tests/PathTracing -p test_sponza_opaque_visibility.py
```

- PR #115統合時のDebug executableを使用。exe/shader/input/source assetのhashを記録。
- 960x540、camera position `(-9, 1.8, 0)`、target `(5, 3, 0)`、FOV Y 65度。
- 一様環境光のみ、Diffuse IBLのみ。直接光・Specular IBL・Hybrid Reflection・RR/SRはOFF。
- 露出1、ACES。raw診断だけtone mapなし。
- ONは64 rays/pixel、pixelごとのsample shift、9x9境界保持filter。
- bias 0.001、TMin 0.0001、trace distance 10000、plane distance 0.005、world radius 0.08。
- raw可視率を確認後、OFF→ON→ON→OFFの順でそれぞれ180 frames後に撮影。
- GPU timestampは10 framesごとに記録し、frame 60未満を除外してmedian/p90を集計。

GPU値はlatest-completed frameのtimestampでありCPU FPSとは区別する。
Debugビルドの短い定常区間の観測で、Releaseの性能認定や他GPUへの一般化はしない。
測定開始前に別のRtPbrSurvey processが起動していないことを確認した。

## 記録

出力先: `bin/PathTracingValidation/sponza-opaque-visibility-r2-20261104/`。
`report.json`にgeometry除外リスト、実行command、hash、GPU計測、Debug Layer警告数を記録。
`comparison.png`は左がOFF、右がON。`raw/capture.png`は加工前の可視率。

最初のrunはfixture helperの戻り値の扱いに誤りがあり、アプリ起動前に停止した。
修正後のfresh outputで再実行した。fixtureの2 unit testsは成功。

## 結果

raw maskにunsupportedを示すmagentaはなく、非一様な可視率を確認した。
通常ONではGPU checkpointにも可視率passが現れ、OFFでは現れないことを検証した。
OFFは室内全体が照らされる一方、ONは柱・アーチの奥や壁面で大きく暗くなる。
これは環境からの直接的なDiffuse寄与の遮蔽であり、多重反射を補うものではない。
正解参照がないため、暗くなったことだけを根拠にGIの正確性を主張しない。

960x540 Debug、各runでframe 60～180の13 samplesによるGPU中央値:

| run | GPU全体 ms | 可視率pass ms | LightPass ms |
| --- | ---: | ---: | ---: |
| OFF A | 11.481 | - | 0.748 |
| ON A | 47.651 | 34.699 | 2.325 |
| ON B | 47.700 | 34.722 | 2.334 |
| OFF B | 11.488 | - | 0.743 |

64 rays/pixelによる増分は約36.2msで、主な負荷はRayQuery約34.7ms。
filterを含むLightPassにも約1.58msの増加がある。画質方式の成立と実用的なGPU予算は
別であり、この条件の64本を安価な既定品質として推奨しない。
次の性能検討候補は低ray数とfilterの組み合わせ、低解像度traceと境界保持upsample。

繰り返しPNGの差はOFFで0、ONで最大1 code値。これは静止画の再現性であり、
camera移動時の安定性の合格ではない。5 capturesはD3D12 ERROR/CORRUPTIONなし。
各runに既存buffer initial-state warningが2件残る。元glTFのhashは変更なし。

一様環境光だけのON画像は暗部の模様を読み取りにくいため、`on-inspect/`へ
露出4の追加captureを用意した。同一露出比較とは別で、性能比較にも混ぜない。

## 手動camera確認

2026-11-04、ユーザーが`on-inspect/scene.json`で起動したMASK除外Sponzaを確認した。
64 rays/pixel、sample shift/filter ON、露出4で左ドラッグにより視線を動かし、
床・柱付近のちらつきやざらつきについて「問題はなさそう」との結果を得た。
この操作範囲で目立つ問題がないという主観評価として記録する。
元のMASKを含むSponza、全camera経路、動的遮蔽物、低ray数の合格を意味しない。
GPU負荷の削減は引き続き別の残課題とする。

## レイ本数の比較

`compare_sponza_visibility_rays.py`で64/32/16本を同じ露出4・960x540・filter設定で撮影した。
記録: `bin/PathTracingValidation/sponza-ray-count-20261104/report.json`。
各120 framesで撮影し、frame 60以降の7 GPU samplesを集計した。

| 本数 | GPU全体 median ms | 可視率 median ms | 64本とのPNG RGB RMSE (0..255) |
| --- | ---: | ---: | ---: |
| 64 | 45.329 | 31.814 | 0 |
| 32 | 28.466 | 15.033 | 2.115 |
| 16 | 26.305 | 10.832 | 3.220 |

64本は正解画像ではなく同条件の比較基準。局所最大code差は32本で74、16本で93あり、
全画面RMSEだけで画質合格とはしない。32本はGPU全体p90が62.9msとなる遅いsampleも
あり、この短いrunは安定性能の認定ではない。D3D12 ERROR/CORRUPTIONはない。
少ない本数ほど静止画像の差が増えるため、まず32本を折衷候補としてcamera移動を
手動確認する。32/16本の移動確認は未実施で、rendererの既定値も変更していない。

## 暗部が見える露出での再比較

露出4ではユーザーが本数による違いを読めなかったため、3条件を共通露出32・ACESで
再撮影した。画像ごとの自動補正やcapture後の輝度変更は行わない。
保存先: `bin/PathTracingValidation/sponza-rays-exposure32-20261104/`。
各本数の`capture-record.json`にcommand/input hash/Debug Layer結果を残す。
これは画質の観察用で、先行のGPU性能比較へ新しい計測値を混ぜない。

`comparison.png`は64/32/16本の縦3段。床・柱・奥壁のcropもそれぞれ同じpixel範囲を
切り出して縦3段にする。`floor-detail.png`、`column-detail.png`、`back-wall-detail.png`は
補間なしの2倍拡大で、新たな細部や平滑化を作り足さない。
明るい天窓側はtone mapの圧縮が強いため、そちらではなく暗部のcropで差を確認する。

## Diffuse/Specular IBLを両方有効にした設定

ユーザー依頼により`prepare_ibl_scenes.py`でCornellとMASK除外Sponzaの別設定を生成した。
保存先: `bin/PathTracingValidation/full-ibl-scenes-20261104/{cornell,sponza}/`。
Diffuse IBL、Specular IBL、SkyboxをONにし、ProceduralStudio環境に空/地面の色差と
明るい光源領域を設定した。直接光とHybrid ReflectionはOFFとし、IBLだけを見る。
可視率は64本＋filter ON、tone mapは両sceneとも露出1・ACES。

これは実HDRファイルではない。現SceneDocument JSONはsource=`procedural`のみを許可し、
Appも読込時にProceduralStudioを指定するため、asset HDRの選択をこの形式には保存できない。
最初に実HDRを使うと述べたが、この制約を確認して訂正した。
RuntimeのAsset HDR対応と、SceneDocumentでの選択/保存対応を混同しない。

非一様な環境を使うため、現在のscalar可視率は方向付きIBL遮蔽の正解ではない。
Specular IBLにも当該可視率を掛けておらず、箱の中の鏡面球が環境を反射する結果は
geometryによる鏡面遮蔽やGIを検証するものではない。元の切り分け用設定は保持した。
