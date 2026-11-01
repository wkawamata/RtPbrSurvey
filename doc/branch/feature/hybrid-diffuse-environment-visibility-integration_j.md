# 分散samplingと空間filterの本体組み込み

## 契約

[実現性検証](hybrid-diffuse-environment-visibility-pattern-study_j.md)で確認したpixelごとの
Cranley-Patterson shiftと9x9 joint bilateral filterを通常のrendererへ組み込んだ。
時間蓄積や別のlighting termへの遮蔽は追加しない。鏡面球の白飛びは別課題。

`DiffuseEnvironmentVisibilitySettings`へ追加したoptional設定:

| field | 既定 | 意味 |
| --- | --- | --- |
| `sampleRotationEnabled` | `true` | pixel座標hashによる2次元sample shift |
| `spatialFilterEnabled` | `true` | LightPassでmaskを境界保持filter |
| `filterPlaneDistance` | `0.005` | 接平面からの距離に対する減衰長、world単位 |
| `filterWorldRadius` | `0.08` | world距離Gaussianのsigma、world単位 |

新しいboolは遮蔽機能全体が有効な場合にだけ効く。機能全体の`enabled=false`と
既定16 rays/pixelは維持し、評価済みCornellでは明示的に64 rays/pixelを指定する。
距離2値は有限かつ正でなければ拒否し、無効なJSONで既存設定を書き換えない。
App/host共通のsettings保存・復元とDebug UIから設定できる。

3m程度のCornellで確認した距離値を一般sceneの万能値とはしない。
大きさが異なるsceneでは上記world単位の値を調整する。自動scale対応は未実装。

## 実装と診断

- compute root constantsを5から6 DWORDへ拡張し、sample shiftの有効値を渡す。
- graphicsは既存22 DWORDのroot constantsのうち4 DWORDを使用し、root signatureの
  総DWORD数は増やさない。新しいtexture、descriptor、historyは追加しない。
- filterは`Shaders/DiffuseEnvironmentFilter.hlsli`。加工前maskをLightPassから読み、
  depth/normal/world positionで重みを付け、Diffuse IBLへ乗算する値だけを平滑化する。
- `Render View = Diffuse Environment Visibility`は引き続きraw maskを表示する。
  filtered maskをraw maskの検証と混同しない。
- sample shiftとfilterを両方OFFにすると、先行実装の固定方向による数値検証ができる。
- CMakeとMSBuildのinclude依存へ追加し、hlsli変更時にもshaderを再compileする。

直接光、Specular IBL、emissive、Hybrid Reflectionの加算項は変更しない。
feature OFFまたはstrength 0ならLightPassでfilterを実行しない。

## 検証記録

ユーザーは隔離した64 rays/pixel試作でカメラを手動移動し、ちらつき・ざらつきが
見られないことを確認した。本体版での同じGUI操作の再確認とは区別する。

`Tests/PathTracing/verify_visibility_integration.py`で本体版を撮影した。
記録: `bin/PathTracingValidation/visibility-integration-r2-20261101/report.json`。

- 遮蔽ONは、承認済み64 rays/pixel試作PNGと全RGBAで完全一致。
- 遮蔽OFFは、導入前の通常PNGと全RGBAで完全一致。
- 最初の比較ではOFFも3 frames warm-upとし、参照120 framesとの間で最大1 code差が
  発生した。同じ120 framesへ揃えたfresh runで完全一致。失敗reportも保持する。

Debug x64 MSBuildとSDK-free CMakeのALL_BUILD（host library/standalone/tests）は成功。
MSBuildには既存MSB4011 warningが1件残る。CTestは通常ユーザー権限で46/46 pass。
CMakeにはimportsのないtest executableに対するLNK4199 warningが2件残る。
最初のsandbox実行ではatomic saveの3件が失敗し、同じbinaryを通常権限で再実行すると
全件成功したため、rendererの修正としてファイル保存処理を変更していない。

新設定のround-trip、無効な0/負数/NaN/Inf、非破壊なJSON拒否をテストした。
GPU raw maskの4条件（開口・閉鎖・無遮蔽・未対応材質）はCPU参照と一致した。
記録: `bin/PathTracingValidation/visibility-integration-mask-20261101/mask-report.json`。
Diffuse合成・強度0・Specular・直接光の10条件も成功した。
記録: `bin/PathTracingValidation/visibility-integration-lighting-20261101/report.json`。
これらの独立CPU参照では明示的にsample shift/filterをOFFにして、固定sampleの
算術的な契約を検証する。新しいshift/filter経路は上記の承認済み試作画像との一致で検証した。
成功したGPU runでD3D12 ERROR/CORRUPTIONはなく、既存buffer initial-state warningは残る。

## 残課題

GPU時間、別scale/scene、動的遮蔽物、連続resizeの確認は残る。
filterはLightPass内の81 tapsであり、性能測定に応じて独立pass等を検討する。
非一様HDRI向けの方向付き遮蔽、GI、反射hit surfaceの遮蔽、反射二重加算は別increment。
