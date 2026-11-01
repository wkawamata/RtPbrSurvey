# Native PT alpha MASK

作成日: 2026-10-25
状態: 実装・自動検証・キャプチャ済み。ユーザー目視承認は未実施。
前段metallic混合と関連修正はcommit `a0245a5`（未push）。

## 契約

`alpha = baseColorTexture.a * baseColorFactor.a` をlinearで計算し、
`alpha >= alphaCutoff` のときだけ表面を残す。
cutoffの既定値は0.5。0はalpha=0も残し、1超は全て切り抜く。
cutoffは非負・finite floatで表現可能な範囲を受理する。
OPAQUEはtexture/factorのalphaを無視する。光学的な透過率Tとは混同しない。

- Native PT限定。active transmissionのBLENDは引き続き拒否する。
- 既存の非透過BLENDは旧来のOPAQUE近似と警告を維持。BLEND対応とは扱わない。
- TEXCOORD_0、RGBA8、共有sampler、mip 0。texture-info変換や別UVは未対応。
- glTF doubleSidedをNative PTの主レイ・可視性レイに伝搬し、裏面法線を向け直す。
- raster/HybridのMASK・両面描画は今回の範囲外で、診断警告に区別を残す。
- Scene EditorへのalphaMode/cutoff編集UIは追加しない。保存したglTF asset参照から復元する。
- volume、BLEND、色付き透過影は対象外。MASKの穴を通る影と光学的な色付き影は別。

## CPU/GPU

GltfMaterial / SceneMaterialにalphaMask・alphaCutoff・doubleSidedを保持する。
node / flattened / legacy経路で伝搬する。GPU flagsへMASKと両面を追加。
GPU Materialは120から124 bytes、alphaCutoffのoffsetは120。
既存のoffsetは保持し、static_assertで検査する。
host側も含め、120-byte版のexe/libraryと124-byte版shaderを混ぜず一緒に再ビルドする。

MASK / 両面 / 薄膜のあるシーンではRayQueryをFORCE_NON_OPAQUEにし、共通candidate判定を使う。
cutoff未満のcandidateはcommitせず探索を続けるため、穴でbounceを消費しない。
普通のopaque-onlyシーンは従来の高速経路を維持する。

対象は主レイ、反射・透過の継続レイ、point等のshadow ray、発光面の可視性、distance query。
発光面NEEではサンプル位置のUVでalphaを評価し、切り抜かれた点のemissionを0にする。
光源CDFは全三角形面積のままなので、細いMASKは棄却が増えるが面積PDFを都合よく変えない。
両面発光材質のsolid-angle PDFはcosineの絶対値を使い、NEEとBSDF-hit側のMISで共通化する。
片面材質は従来通り裏面のPDFを0とする。GPU契約テストは正面/裏面のPDFを独立に確認する。
材質境界での異なるdescriptor indexを扱うため、albedo/emissive/normalの参照にも
NonUniformResourceIndexを揃える。

## NRD

MASKのNRD品質は未認定のため、要求時は
`Native fallback: alpha MASK is not qualified for NRD` を表示する。
実行グラフ・conversion・evaluate準備を全て止め、履歴をresetし、Native PTを表示する。
MASKシーンのdistance diagnosticsも未使用とする。通常の両面材質だけなら既存distance queryを使う。

初回実描画では11ケースが通った後、NRD fallbackの契約で不合格となった。
表示と可視化側は停止していたが、BuildRenderPassesの診断実行と履歴初期化が薄膜だけを
除外しており、MASKでもNRDのLastEvaluation.recordedがtrueになっていた。
同じ除外条件にMASKを追加し、判定を緩めず再実行して19ケース全て合格した。
初回証拠は `bin/PathTracingValidation/alpha-mask-20261025/report.json` に保持する。
合格証拠は `bin/PathTracingValidation/alpha-mask-r2-20261025/report.json`。
NRDのplanned/prepared dispatchは0、LastEvaluation.recordedはfalse。

## 検証

- [x] Debug x64ビルド成功。最終incremental buildは警告0・エラー0。
- [x] CTest 43/43。Alpha MASKのGPU契約テストを追加。
- [x] 28,672比較でfactor、cutoff等号、cutoff=0/1超をWARP / hardwareで確認。
- [x] Python: 既存透過17、粗さtexture4、MASK5、計26/26。
- [x] 実描画19ケース。D3D12 Debug LayerのERROR/CORRUPTIONなし。
- [x] 既存の不透明・透過回帰9ケース。不透明の旧ビルド対照は画素一致。
- [x] 既存の金属混合回帰8ケース（両面発光PDFの最終修正前）。
- [x] 両面発光2ケースを追加した実描画21ケースの最終再実行。
- [x] 保存シーン26のキャプチャ。1920x1080、256 samples、8 bounces。
- [x] 原版TransmissionTestの材質を変更しないruntime smokeとキャプチャ。
- [ ] ユーザー目視承認。

実描画ケース:

- mask / scaled / zero-cutoff / high-cutoff / opaque。
- back-one-sided / back-double-sided / thin。
- transmission-layer / reflection-layer / constant-equality / nrd-fallback。
- point-clear / point-hole / point-solid。
- emitter-clear / emitter-hole / emitter-solid / masked-emitter。
- emitter-back-one-sided / emitter-back-double-sided。

主レイはUnlit表面と既知のUnlit背景を使い、テクセル境界から離れた4 ROIで独立にcoverageを計算。
確定的なRGBは画素ごとの最大絶対誤差0.001。roughness=0でもalpha下限を持つ既存金属BSDFで
鏡面反射越しに見るケースだけはROI平均0.003を実行前に固定する。
point/emitterの穴は照明ありのclear対照と最大差0.001、solidと全切抜き発光面は最大値1e-7。
発光面はNEE-onlyで2三角形のuploadを要求し、MASKで光源テーブルを消して成功させない。
NRDはplanned/prepared dispatch=0とLastEvaluation.recorded=falseを要求する。
裏向き片面発光はROI内で0、両面発光は64 sppの表向き対照に対しROI平均RGBの相対誤差0.03以内。
発光面の反転で面積サンプル位置が変わるため、この比較は画素一致を要求しない。
全case名の一致、実行時状態、入力・RAW・ログ・exe・PT shader・scriptのhashを保持する。

点光源と発光面のhole対照は、それぞれclear対照とROI内で画素一致。
solid対照と全切抜き発光面はROI内のRGB最大値0。
裏向き片面発光も最大値0。両面の裏向き発光は表向き対照に対しROI平均RGBの
最大相対誤差0.00005752で、事前予算0.03以内。
最終証拠は `bin/PathTracingValidation/alpha-mask-r3-20261025/report.json`。
透過回帰は `bin/PathTracingValidation/alpha-thin-regression-20261025/report.json`。
金属混合回帰は `bin/PathTracingValidation/alpha-metallic-regression-20261025/report.json`。
金属混合回帰の実行後は両面発光PDFとbuild dependencyのみを修正した。
NRDの全画像品質スイート、SDKなし構成、raster/HybridのMASK描画は今回の検証対象外。
全画像・RAW・ログは生成物としてローカルのbinに置き、Gitには含めない。

保存シーン26は左OPAQUE / 中央MASK / 右MASK+薄膜透過。
alphaは0/64/128/255、cutoff=0.5。RGBは青。自作geometryと小さなPNGのみをGit管理する。

最終ビルドはinclude依存関係の追加後も警告0・エラー0で成功し、exe / PT shaderのhashは
21ケースのレポートと完全一致した。

- exe SHA256: `e332a865f144bda7dfc8bdc4c2b274897f6999a543018cbe991e62df3fbc2adc`
- PT shader SHA256: `aef4ddd446e4723a86715009fa6ac77ad5884be0482635cba9e777e02b795542`
- scene26: `bin/PathTracingValidation/alpha-mask-review-20261025/report.json`
- 原版: `bin/PathTracingValidation/transmission-test-original-review-20261025/report.json`

## 原版TransmissionTest

`C:\work\glTF-Sample-Models\2.0\TransmissionTest\glTF` をbin内へコピーし、全ファイルのhash一致を
`bin/PathTracingValidation/transmission-test-original-20261025/source-manifest.json` に保存した。
原版のgeometry / material / textureは変更せず、SceneDocument wrapperと照明presetだけを追加した。
glTF内カメラの位置・回転・FOVをLHカメラへ変換して使用した。
元glTFのSHA256は `8f202a2fa3139f8f3cffd116a63aa951a762a49316ded72aa09f1fbbe389acc9`。

1920x1080、256 samples、8 bouncesでruntime smoke合格。Debug LayerのERROR/CORRUPTIONなし。
これは材質保持と実行の確認であり、Khronos参照画像との一致やユーザーの品質承認ではない。
粗さ・metallic・透過texture・MASKの組合せは通るが、レンダリングは依然として薄膜近似。
体積屈折・吸収・色付き透過影の追加をこの結果で代替しない。
外部モデル、生成したwrapper、画像・ログはGit管理に含めず、公開もしない。

## 再実行

出力先は未作成ディレクトリを指定し、GPUキャプチャを同時実行しない。

```powershell
ctest --test-dir bin/PathTracingValidation/thin-gltf-build-20261024 -C Debug --output-on-failure
python -B Tests/PathTracing/validate_alpha_mask.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --output bin/PathTracingValidation/alpha-mask-new-run
python -B Tests/PathTracing/capture_thin_visual_comparison.py --exe bin/PathTracingValidation/thin-gltf-build-20261024/Debug/RtPbrSurvey.exe --scene Assets/Scene/PathTracingValidation/26-alpha-mask-thin-transmission/scene.json --output bin/PathTracingValidation/alpha-mask-new-review --frames 256
```

## 参照

- [glTF alpha coverage](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc#alpha-coverage)
- [glTF alphaCutoff schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json)
