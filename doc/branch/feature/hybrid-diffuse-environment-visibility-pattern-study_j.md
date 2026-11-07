# 環境可視率の模様を抑制できるか

## 目的と境界

Cornell内に現れた大きな四角形・段階状模様を、現在のRayQueryによる環境可視率方式の
まま抑えられるか調べる。鏡面球の過剰な明るさと間接反射不足は別問題として変更しない。
本体のshader/public API/既定設定には試作を適用せず、隔離したruntimeで比較する。

対象は一様環境・opaque geometry・静止カメラ。高サンプルの環境可視率を参照に使うが、
PT/GIの正解画像、時間方向の安定性、性能の合格判定には使わない。

## 原因と試作

現状は全pixelで同じHammersley方向を使い、64本なら可視率が65段階になる。
遮蔽物の輪郭が同じ方向へ投影されるため、誤差が大きな面状の模様として揃う。
単純なblurはこの低周波の誤差を十分に消せず、接地影をぼかす可能性がある。

試作はpixelごとの固定hashで2次元sample列をCranley-Patterson shiftする。
固定seedとし、frameごとのrandomizationやtemporal historyは追加しない。
次にmaskだけを9x9のjoint bilateral filterで処理し、spatial Gaussian、法線の一致度、
接平面からの距離、world-space距離で重みを落とす。最終色や直接光をblurしない。

研究用filterは3mのCornellに合わせたworld-spaceしきい値を使う。
一般sceneへ採用する前にpixel footprint/world scaleへの対応とGPU時間の測定が必要。
LightPass内で直接9x9を読む実装は実現性の検証用で、最終的なpass構成ではない。

## 比較条件

- 固定64本、filterなし。
- 固定64本、filterあり（blurだけで改善するかの対照）。
- 分散64本、filterなし（粒状noiseへの変化を確認）。
- 分散64本、filterあり。
- 分散64本、別seed、filterあり（sample列依存性の確認）。
- 固定1024本と4096本、filterなし（有限sampleの参照差）。

カメラ・材質・光・露出は前の外側Cornell構図を維持する。
通常PNGと、同じLightPassで合成直前のmaskを返したlinear EXRを別processで取得する。
maskはTinyEXRでdecodeしてfloat32へexportし、壁・床・拡散球の接地付近のROIで測る。
成功条件を画像の見た目だけで決めず、参照との差、平均値の偏り、境界の保持を確認する。
4096本も有限sampleなので、1024本との差を参照精度の目安として併記する。

## 再現

```powershell
python Tests/PathTracing/study_visibility_patterns.py --source bin/PathTracingValidation/cornell-ibl-visual-square-20261101/environment/hybrid-on --output bin/PathTracingValidation/visibility-study-new --decoder build/cmake-host-debug/Debug/RtPbrSurvey.DiffuseEnvironmentVisibilityTests.exe
```

`--size 720 --variants fixed64 scrambled64-filter reference4096`で主要3条件を拡大確認できる。
shader variant、command、shader hash、EXR数値の比較結果は出力先の`report.json`へ記録する。
runtimeのshaderを差し替えるのは当該出力先だけで、`bin/x64/Debug`のshaderは変更しない。
Assetsはread-only用途のhard linkで複製する。試作はAssetsへ保存処理を行わない。

最初の隔離runは検証fixtureに絶対asset pathを指定し、SceneDocumentの相対path契約に
違反して起動失敗した。相対pathへ修正したfresh outputで再実行する。失敗結果も保持する。

## 結果と判断

360x360の7条件・14 captures:
`bin/PathTracingValidation/visibility-pattern-study-r2-20261101/report.json`。
720x720の主要3条件・6 captures:
`bin/PathTracingValidation/visibility-pattern-study-720-20261101/report.json`。
720版の`comparison.png`は左から現状、分散64本＋filter、4096本参照。

720版のlinear maskで測定した4096本参照との差:

| ROI | 固定64本 RMSE | 分散64本＋filter RMSE | RMSE減少率 | 試作の平均偏差 |
| --- | ---: | ---: | ---: | ---: |
| 奥の壁 | 0.021095 | 0.002631 | 87.5% | -0.000046 |
| 床 | 0.012556 | 0.002117 | 83.1% | -0.000344 |
| 拡散球の接地付近 | 0.015129 | 0.003164 | 79.1% | -0.000069 |

360版では固定64本にfilterだけを加えても壁のRMSEは0.021159から0.019653に
留まった。一方、分散64本＋filterは0.002865となった。単なるblurではなく、
空間的に揃ったsample誤差を分散することが大きな模様の抑制に重要だった。

360版の1024本参照と4096本参照のRMSE差は、壁0.001922、床0.001056、
接地付近0.001633。4096本も厳密解ではないが、固定64本の大きな模様と試作の差は
この有限sample差より十分に大きい。

別seedの分散64本＋filterでも壁のRMSEは0.002765で同程度だった。
seed間RMSEは壁0.003816、床0.002756、接地付近0.003938で、平均偏差は最大0.000501。
これはsample列依存の残差を確認した値であり、移動時の安定性を検証した値ではない。

通常表示でも大きな四角形・段階状模様は抑えられ、接地付近の暗さは保持された。
ただし4096本参照より細かなざらつきは残る。鏡面球の過剰な明るさは変更していない。

**現段階で別方式へ切り替える必要はない。現在の可視率方式でも模様を大幅に抑制できる。**
これは静止Cornellでの実現性の判断であり、製品品質の合格ではない。
本体採用時はraw/filtered maskの診断を分離し、world-scaleへの対応、filterのpass構成、
連続camera/object移動時のちらつき、GPU時間を確認する。4096本を本番採用する提案ではない。

研究shaderはDXCでcompileし、20 capturesはD3D12 ERROR/CORRUPTIONなし。
各processに既存buffer initial-state warningが4件あり、warning-freeとはしていない。
EXR exporterを含むCMake test targetのMSBuildは0 warnings / 0 errorsで成功し、
既存の環境可視率reference CTestは1/1 pass。今回の調査では本体C++とproduction shaderを変更していない。

## 未検証

その後の本体組み込みは[組み込み記録](hybrid-diffuse-environment-visibility-integration_j.md)を参照。

連続camera/object移動、disocclusion、resize、別scale、薄い形状、非一様HDRI、GPU時間。
静止画の改善だけで「出荷可能」「temporalも安定」「PTと一致」とは判断しない。
