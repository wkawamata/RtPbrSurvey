# Native PT 透過・屈折の完成に向けた作業

作成日: 2026-10-31

ブランチ: `codex/pt-transmission-completion`

開始ベース: `12912e2` (PR #109)。HybridのDiffuse改善とSponzaの照明調査は保留する。
シーン10のユーザー編集と生成物は、この作業へ取り込まない。

## 完成の段階

「限定条件のガラスが動く」と「汎用glTF・複合媒体・透過光まで完成」は分ける。
まず単独の閉じたガラスについて、滑らか／粗い境界、屈折率、実距離による吸収を
整合したモデルで扱う。次に媒体とglTFの対応範囲を広げる。

最終目標は、ステンドグラスを通った色付き照明。単に背景が透けることや、直線の
色付きシャドウ近似だけを、その目標の達成とはしない。

## 開始時点の実装

- 薄膜の透過率・透過色・粗さテクスチャ、金属度との混合、Alpha MASK。
- 単独の閉じた凸形状など、限定された滑らかなVolume境界の屈折・全反射と距離吸収。
- Scene Editorと限定glTF入力。未対応形状・組み合わせを診断して拒否するガード。
- 厚みは実際の境界間距離から求める。glTF thicknessFactorを光路長へ重ねて掛けない。

既存の詳細と検証履歴: [Volume透過](path-tracing-volume-transmission.md)。

## 実施順序と完了条件

### 1. 粗いVolume境界の数学契約

- [x] 独立した粗い誘電体境界のサンプラとBSDF/PDF評価を追加。
- [x] 空気からガラス／ガラスから空気、斜入射、全反射、同一IOR、roughness=0を検証。
- [x] 反射と屈折のPDFの測度、Fresnel選択、屈折Jacobian、radianceのeta補正を照合。
- [x] 不正入力、非有限値、マクロ面の誤った半球へ出るイベントの拒否を検証。

この段階ではランタイムやglTFの粗さ制限を解除しない。独立GPU契約テストの成功を
実描画や全エネルギー収束の承認と混同しない。

### 2. 単独Volumeの実描画へ接続

- [x] 入口／出口ごとに正しいIORを使い、反射は媒体を保持、屈折だけが媒体を変更。
- [x] 既存の内部光路長・吸収・輪郭対策と接続する。roughness=0は既存のdelta経路を維持。
- [ ] 粗いCube／Sphere、入射角、厚さと吸収の比較シーンをAssetsへ保存。
- [ ] 半球拒否・光路終了・媒体不整合を見える形で診断し、数値検証と目視確認。
- [x] 薄膜、MASK、不透明、滑らかなVolumeの既存回帰を基準を緩めず再実行。

### 3. Scene Editorと限定glTFの拡張

- [ ] 検証済みの粗いVolume材質を編集・保存・再ロード可能にする。
- [ ] 既存の拒否理由を一つずつ解除し、原版の材質を保持したglTFで確認する。
- [ ] 材質テクスチャや複数プリミティブ等は、受理条件と実装を合わせて段階的に追加。
- [ ] 対応しない拡張や材質の組み合わせを、黙って薄膜・不透明へ読み替えない。

### 4. 幾何と媒体の一般化

- [ ] 分離した複数のガラス、凹形状、内部カメラ、入れ子・重複媒体の順序と設計を確定。
- [ ] 必要な媒体スタック、インスタンスと境界の識別、内外判定を独立検証。
- [ ] ガードを外す前に、それぞれの形状・配置に対応するテストと診断を追加。

この段階は現行の単一媒体モデルを越える。単なる上限解除では実装しない。

### 5. ステンドグラスの色付き透過光

- [ ] 色付きvisibility近似を中間機能とするか検討し、物理的な屈折光路とは表示上も区別。
- [ ] 屈折した直接光・コースティクスを効率よく標本化する方式を選定。
- [ ] 受光面の色、光量、屈折による位置変化を独立な基準と比較。
- [ ] 最終目標の確認シーンと日本語の観察項目をAssetsへ保存。

### 6. 再構成と受入

- [ ] Nativeを基準に、透過・VolumeのRR/NRD対応を個別に検証する。
- [ ] 屈折像・厚さ依存の色・内部反射・動作中の履歴をユーザーが目視確認。
- [ ] 対応範囲、既知制約、未対応glTF拡張を整理し、その範囲に対して完成を判定。

## Step 1の実装方針

既存の滑らかな境界サンプラとGGX visible-normal helperを再利用し、独立の
`Shaders/RoughVolumeTransmission.hlsli`に実装する。ローカル法線は+Z、入射レイは
-Z側へ進み、viewと評価する出射方向は境界から外向きとする。

- roughness=0、または両側IORが等しい場合はdeltaイベント。solid-angle PDFは0とし、
  離散イベント確率を別に保持する。
- 粗い境界はGGX visible-normalとFresnelによる反射／屈折選択を使う。
- `etap = transmittedIOR / incidentIOR`。屈折PDFには、半角ベクトルから出射方向への
  Jacobianを含め、radianceの透過BSDFには`1 / etap^2`を含める。
- 分離型Smith maskingを使用する。サンプル重みは反射がG1(out)、屈折が
  G1(out) / etap^2。独立なBSDF * abs(cos) / PDFとも照合する。
- GGX alpha下限は既存helperと同じ0.001。低粗さの下限改善、多重散乱補償、
  NEE/MISによる媒体越しの光源接続、幾何法線とシェーディング法線の補正は別途評価。

試験は7つの粗さ・IOR組と4つの入射角、各1,024サンプルの計28,672イベント。
CPU側は倍精度のSnell/Fresnel、VNDF基準、再構成半角ベクトルによるBSDF/PDFを用いる。
方向等は既存テスト同様の`0.0002 + 0.002*abs(reference)`、粗いBSDF/PDFは
`0.0002 + 0.005*abs(reference)`を初期基準とする。失敗時に結果に合わせて緩めない。

式の確認: [pbrt Dielectric BSDF](https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)。
材質パラメータのマッピングやモデル全体をpbrtと同一とするものではない。

## Step 1の結果

- CMake Debug x64ビルド成功、警告0・エラー0。
  外部ビルド記録: `C:/work/RtPbrSurvey-agents/pt-rough-volume-contract-final-20261031/`。
  無効サンプルの最終出力クリア後のテストターゲット再ビルドも成功:
  `C:/work/RtPbrSurvey-agents/pt-rough-volume-invalid-output-20261031/`。
- CTestは55/55合格。新しい`RtPbrSurvey.RoughVolumeTransmission`を含む。
- 実機とWARPの双方で28,672サンプル合格。各々、反射4,611、屈折23,683、
  マクロ面の半球による拒否378。D3D12エラーなし。
- 入射cosは1、0.8、0.6、0.1。粗さは0、0.1、0.35、0.7、1の組み合わせと、
  内部からの出射、同一IORを対象とする。これ以外の全入射角・IORを保証しない。
- 乱数上限1、負／過大な粗さ、非単位ベクトル、IOR=0、NaN IOR／乱数、
  不正な方向乱数を拒否。無効サンプルには重み・PDF・方向を残さない。
- 証拠: `bin/PathTracingValidation/rough-volume-contract-20261031/`の
  `hardware.log`、`warp.log`、`ctest.log`、`evidence.json`。
  evidence.jsonはハッシュの索引であり、独立した合否集計器ではない。

今回のhelperはテスト用シェーダーからのみ使用する。アプリのPTシェーダー、
Scene Editor、glTF受理条件にはまだ接続していない。したがって粗いVolumeの
実画像が完成したことや、連続角度のPDF積分、炉試験、相反性、媒体越しの
NEE/MIS、再構成品質を検証したことは主張しない。アプリ実描画を変えていないため、
今回は新たなVolume画像一式や通常vcxprojビルドを再実行していない。

次はStep 2として、限定された単独Volumeでランタイム接続と実画像評価へ進む。

## Step 2のランタイム接続

- 粗さ0の既存delta経路を保持し、粗さが正の場合だけ幾何法線のローカル座標で
  GGX VNDF境界を標本化する。媒体は屈折時のみ変更、反射時は保持する。
- 内部の物理的な区間長、Beer-Lambert吸収、前境界の除外規則は変更しない。
- マクロ半球による棄却はゼロ寄与の光路終了。Volume StatusのR=7で区別する。
  これはバウンス不足(R=3)や媒体不整合とは別のサンプリング結果である。
- SceneDocumentの単独Cube/Sphereについて粗さ制限を解除する。他の制限や
  glTF入力のsmooth限定は維持する。Editorの粗さ操作は後述のStep 3で解放する。
- 保存シーン33は粗い箱、34は粗い球。roughness=0.35、IOR=1.5、色付き距離吸収。
  Scene 28/30の滑らかな材質と背景格子のぼけ方を比較する。
- `validate_rough_volume_runtime.py`はCube/Sphere各々のroughness=0/0.35を
  32サンプルで実行し、実行状態、非有限値、負値、中央の黒潰れ、画像差を確認する。
  これはsmoke試験であり、基準画像との一致やエネルギー保存の証明ではない。

保存シーンの目視承認、粗い境界の全角度・厚さ・吸収の数値比較と、薄膜/MASK/吸収/
glTFのGPU画像回帰一式は引き続き必要。Step 2全体を完了扱いにはしない。

### 接続時点の確認

- 通常vcxproj Debug x64とCMake Debugのビルド成功。通常ビルドは既存のvcpkg
  MSB4011警告1件。最終通常ビルド記録は外部の
  `pt-rough-volume-runtime-final-20261031`フォルダー。
- CTest 55/55合格。初回はsandbox外の一時ファイル保存に関する3試験が失敗したが、
  書込み可能な実行で全件合格。その後追加した保存シーン33/34の構築試験も合格。
- 診断値7追加後の実描画4ケースが合格。証拠は
  `bin/PathTracingValidation/rough-volume-runtime-diagnostic-final-20261031/report.json`。
- 保存シーン33/34の実行・PNG出力は終了コード0、D3D12エラーなし。
  32サンプルの画像で格子のぼけと着色を確認。ユーザーの目視承認とは区別する。
  出力は`rough-volume-runtime-final-20261031/33-rough-volume-cube.png`と
  `34-rough-volume-sphere.png`。
- 滑らかな境界回帰は先行9ケース合格後、通常ビルドのNRD設定が専用試験の
  前提と異なり`nrd-fallback`で停止。回帰全体は未合格。
  証拠は`rough-volume-smooth-regression-20261031/report.json`。
- 新規保存JSONの初回確認では必須camera/environment項目の不足が判明したため修正。
  保存シーンのスキーマ・構築回帰を追加し、修正後の実行と出力を確認した。

### 光路別吸収・厚さ・角度の検証

`validate_rough_volume_absorption.py`でroughness=0.35、IOR=1.5のCube正面、
厚さ2のCube斜入射、Sphereを実行する。各ケースは内部距離、透過率、状態の
3キャプチャを同じシーン・乱数・サンプル番号で取得し、64x64 ROIを照合する。
蓄積値から平均光路長を逆算せず、単一サンプルの区間距離に対して
`attenuationColor^(distance/attenuationDistance)`をCPUで計算する。

- 3ケース・9キャプチャ合格。最大誤差は約2.27e-7、固定許容値0.002。
- 完了した入出射光路は各ROIで3,861 / 3,855 / 3,850本。
- マクロ半球による棄却は64 / 68 / 74本。媒体・境界エラーはなし。
- バウンス上限到達は0 / 0 / 1本。打切りを隠さず、部分光路の吸収とは区別する。
- 証拠: `bin/PathTracingValidation/rough-volume-absorption-20261031/report.json`。
- 判定器には誤吸収、媒体エラー、棄却のみ、NaN、寸法不一致の拒否テストを追加。
  新規6件を含むVolume Python試験25/25合格。CMake再ビルドとCTest55/55も合格。

この照合は実際の区間距離と吸収の接続を検証するもので、全粗さ・全角度の幾何学的
光路長の独立解、エネルギー積分、コースティクス、RR/NRD品質の検証ではない。

NRDありのCMake最終構成で滑らかなVolume境界回帰を再実行し、12/12合格。
NRDフォールバックと全反射、バウンス上限の対照も既存基準のまま合格した。
証拠: `bin/PathTracingValidation/rough-volume-boundary-regression-sdk-20261031/report.json`。
通常ビルドでの先の失敗記録は保持し、この別構成の成功で上書きしない。
既存の吸収18件・glTF6件・薄膜/MASKのGPU画像回帰一式はまだ再実行していない。

## Step 3: プリミティブの粗さ編集

Cube/SphereのVolume材質でRoughnessスライダーを有効にする。既存の編集通知、
dirty表示、PT履歴リセット経路をそのまま使用し、金属度・境界色の無効化は維持。
粗さ0.35のVolumeについて、Undo/Redoとシリアライズ再読込の回帰試験を追加した。
SceneEditorSession試験合格、通常Debug x64ビルド成功（既存MSB4011警告1件）。
GUIを実操作した目視確認は未実施。粗いglTF Volumeの受理条件は解除していない。

### 既存材質への回帰確認

NRDありCMake構成の同一バイナリを使用する。基準値は変更しない。

- glTF Volume 6/6合格、grazing/missed-exitは0、独立な幾何光路長の最大誤差は
  約1.42e-5（許容0.005）、吸収最大誤差は約1.59e-7（許容0.002）。
  Bias/TMin拡大時の透過率差とNRD bypass差はともに0。
  証拠: `rough-volume-existing-gltf-20261031/report.json`。
- RR bypassは今回の6件には含めない。RRの対応範囲をこの結果から拡大しない。
- 薄膜の基本統合は8キャプチャ合格。無色、着色、裏面、NRD bypass、二重薄膜、
  factor=0と通常不透明材質の一致を確認。過去バイナリとの不透明画像比較用
  `--opaque-reference-exe`は指定していない。
  証拠: `rough-volume-existing-thin-20261031/report.json`。
- 透過関連Python17件とAlpha MASK判定器Python5件も合格。
  Alpha MASK GPU画像21件、粗い薄膜の炉試験、各テクスチャGPU画像回帰一式は
  今回まだ再実行していない。これらを基本薄膜8件の成功で代用しない。
- 既存Volume吸収18/18合格。厚さ2倍、斜入射、スケール、無限距離、黒い吸収成分、
  球、内部反射・全反射、Fresnel付き放射輝度、NRD bypassを既存基準で確認。
  証拠: `rough-volume-existing-absorption-20261031/report.json`。
- Editor試験更新後のCTestも55/55合格。

### 薄膜の炉試験の再確認

既存の`validate_rough_transmission.py`を128サンプルのまま実行し、4/4合格。
滑らかな薄膜は期待値1に一致。粗さ0.5の期待値は約0.915812、実測約0.914411、
粗さ1の期待値は約0.306853、実測約0.305165。既存の標本誤差許容は約0.018972。
環境NEEを使わないモード間の粗さ0.5の画像も完全一致した。
証拠: `rough-volume-existing-thin-furnace-20261031/report.json`。
これは薄膜モデルの炉試験であり、新しい粗いVolume境界の炉試験ではない。

### Alpha MASKのGPU回帰

既存の`validate_alpha_mask.py`を変更せず実行し、21/21合格。
一次レイの閾値・係数・片面/両面、薄膜/反射の継続レイ、NRD bypass、
点光源と発光面のvisibility、マスク付き発光面、発光面の裏面条件を確認した。
D3D12エラーなし。今回の回帰で新しい不具合は見つからず、実装修正はしていない。
証拠: `rough-volume-existing-mask-20261031/report.json`。

これで既存境界12件、吸収18件、glTF Volume6件、基本薄膜8キャプチャ、
粗い薄膜の炉試験4件、Alpha MASK21件を再確認した。
透過率/透過色/粗さテクスチャと金属度混合のGPU画像回帰一式、粗いVolume自体の
炉試験、Editor粗さ操作の目視確認、粗いglTF Volumeは引き続き未完了。

### 透過率・色テクスチャのGPU回帰

`validate_transmission_textures.py`を既存の64サンプルで再実行し、6/6合格。
色、透過率、係数との積、率と色の併用、画像共有、NRD fallbackを確認した。
証拠: `rough-volume-existing-textures-20261031/report.json`。
今回の回帰では透過テクスチャの実装変更や許容値変更は行っていない。

### 金属度混合のGPU回帰

`validate_metallic_transmission.py`を既存の128サンプルで再実行し、8/8合格。
非金属、金属度0.5、金属、通常不透明対照、金属度テクスチャ、ガイド、金属の裏面を
確認した。証拠: `rough-volume-existing-metallic-20261031/report.json`。
これも薄膜側の既存対応の回帰であり、金属とsolid Volumeの混合を許可するものではない。

### 粗さテクスチャのGPU回帰

`validate_roughness_texture.py`を既存の128サンプルで再実行し、6/6合格。
粗さガイドの値、係数0/0.5/1との積、空間変化、炉試験を確認した。
Pythonの判定器/fixture試験も4/4合格。
証拠: `rough-volume-existing-roughness-texture-20261031/report.json`。

予定した既存材質のGPU回帰は一巡し、新しい不具合や基準の緩和はなし。
次は単一凸形状の限定glTF Volumeで定数粗さを受理し、読み込んだ粗さがランタイムへ
伝わることと、光路・吸収・境界診断を検証する。テクスチャ、金属、部分透過、複合媒体は
この受理条件の変更へ同時に含めない。粗いVolumeの炉試験とGUI目視確認は引き続き別途必要。

## Step 3: 限定glTF Volumeの定数粗さ

glTFローダーのsolid Volume条件から`roughnessFactor == 0`だけを外す。
粗さの有限値/[0,1]チェック、非金属、完全透過、白い境界、IOR上限、テクスチャ等の
既存材質条件と、単一閉凸形状・配置・カメラのガードは変更しない。
0.1/0.35/1.0の定数粗さとVolume識別がランタイム材質へ保持されること、
範囲外の粗さを拒否することをC++試験へ追加した。

- 保存シーン35 `35-gltf-rough-volume-tetra`を追加。粗さ0.35、IOR=1.5、
  距離吸収はシーン31と同じ。格子像のぼけを比較する。
- 通常Debug x64ビルド成功（既存MSB4011警告1件）、CMake Debugビルド成功
  （テストターゲットの既存LNK4199警告3件）。CTestは最終55/55合格。
  初回は新規拒否試験が一時ファイルを不正値のまま残したため後続試験が失敗した。
  smooth fixtureの書戻しを追加して再ビルド・全件再実行済み。
- `validate_gltf_rough_volume.py`でroughness=0/0.35の光路距離・透過率・状態を
  各々取得する6キャプチャが合格。画面全体で状態コード0/3/7以外を拒否する。
  中央64x64の完了入出射は3,935/3,721本、棄却0/202本、上限到達1/0本。
  距離から独立計算した吸収の最大誤差は約2.88e-7/2.60e-7（許容0.002）。
  共通完了光路177,865本で平均絶対距離差は約0.747。粗さが光路を変えることを確認。
  証拠: `bin/PathTracingValidation/gltf-rough-volume-20261031/report.json`。
- 滑らかなglTF Volumeの既存6件も再実行し合格。missed-exitは0、独立な幾何光路長
  誤差約1.42e-5、Bias/TMin拡大時の透過率差0。
  証拠: `gltf-rough-volume-smooth-regression-20261031/report.json`。
- 保存シーン35を通常Debug版で64サンプル描画し、PNG出力と終了コード0を確認。
  D3D12エラーなし。画像で格子像のぼけと青い着色を確認したが、ユーザー目視承認は未実施。
  画像: `gltf-rough-volume-20261031/saved-scene.png`。

任意のglTFモデル、複数Primitive、凹形状、媒体重複、粗さテクスチャのsolid Volumeを
受理したわけではない。既存の3モデル全体やユーザーの目視承認、粗いVolume自体の
エネルギー積分を、この限定試験の成功から保証しない。

## Material ControlsでのVolume調整

Volume材質にVolume IOR、Absorption Color、Absorption Distance、
Infinite Absorption Distanceを追加。IORは[1,4]、吸収色は[0,1]、有限距離の
スライダーは[0.001,1000] world units。無限距離へ切り替えると吸収しない。
有限へ戻す際の初期値は1。Volumeの粗さは0まで指定可能とし、金属度は編集不可。
表示する誘電体F0も現在のIORから計算する。

変更は既存の材質バッファ更新経路で即時反映し、PT履歴をMaterial変更としてリセット。
更新APIは明示的なVolume更新フラグを使い、既存呼出しが既定IOR/吸収値を上書きしない。
API側でもIOR・吸収色の非有限/範囲外、距離のNaN/ゼロ以下を拒否する。
実行中の調整のみであり、元glTFやScene Editor文書への保存は行わない。
GUIのスライダー操作による反映・履歴リセットはユーザー目視確認が必要。

通常Debug/CMake Debugビルド成功、CTest55/55合格。
通常Debugでシーン35の起動・8サンプル描画・PNG出力が成功し、D3D12エラーなし。
証拠は`gltf-rough-volume-20261031/material-controls-startup.log`と同名PNG。
この起動確認はGUIを操作して値変更と履歴リセットを検証したものではない。

## 保存シーンのTurntable

2026-11-01: 任意の`turntable`設定をSceneDocumentへ追加。対象のルートMeshノードID、
world-spaceのpivot、Y軸degreesPerSecondを保存する。初期対応は子を持たない単一ノード。
既存の設定なしシーンは静止したまま。シーン35はsolidを原点周りに15度/秒で回転する。
シーン35の四面体は正四面体ではなく、辺長1とsqrt(2)を持つ直交四面体の拡大形。

通常実行のMaterial ControlsにPlay Animation/Pause Animationを表示し、既存のSpaceと
同じ再生フラグを操作する。初期停止からPlayで開始。Pauseは角度を保持し、PT蓄積自体は
止めない。回転中は既存のSetScene経路がScene変更としてPT蓄積をリセットする。
停止時もprevWorldを現在行列へ揃える。Resetは元の姿勢へ戻す。
対象ノード削除時にはTurntable参照も削除する。

通常Debug/CMake Debugビルド成功、CTest55/55合格。追加CPU試験で回転・停止時の行列、
前フレーム行列、Reset、1回転中のVolume配置ガード、設定の保存再読込、対象削除を確認。
GUIボタン/Spaceの実操作と回転中のGPU描画の目視確認は未実施。

## 静止した四面体の反射・屈折像の調査

2026-11-01: ユーザーから、Pause後の透明な四面体に断片状の像が見えるとの報告。
GUIのIOR/粗さ/吸収設定はまだ不明。見た目だけで正常・不具合とは判定しない。

`validate_smooth_volume_paths.py`を追加し、独立したCPU倍精度の平面交差、
Snell/Fresnel、全反射、内部反射の追跡とGPU診断を比較。
共有するのは乱数列とカメラ設定であり、CPU側はHLSLサンプラを呼ばない。
IOR=1.5、roughness=0、固定姿勢0度/60度、各1024画素・3キャプチャが合格。
最大内部距離誤差は約1.44e-5、状態/反射回数差0、出射方向誤差は最大約1.27e-4。
初期固定許容値は距離0.005、状態0、方向0.002で、失敗に合わせた緩和なし。
最初の実行は診断側がサンプル番号0を仮定したため失敗し、キャプチャメタデータの
sampleStartIndex=1を参照するよう修正。失敗記録は保持し、新規フォルダーで再実行した。

証拠:
- `smooth-volume-path-reference-20261101/report.json`: 診断側の初回失敗。
- `smooth-volume-path-reference-final-20261101/report.json`: 固定0度合格。
- `smooth-volume-path-reference-angle60-20261101/report.json`: 固定60度合格。

これは中央ROIの光路比較であり、放射輝度画像全体、全姿勢、全IOR/粗さや、
ユーザーが停止した姿勢・設定を直接再現した試験ではない。レンダラーの変更は未実施。
ユーザー設定の確認と、必要ならIOR=1/粗さ0/吸収なしの直進透過対照から再現を続ける。

## Material Controlsのユーザー受入結果

確認日: 2026-11-01。シーン35でのMaterial ControlsへのIOR・吸収色・吸収距離の
追加は、ユーザーが動作確認し、成功として受入済み。シーン35での評価はここで区切る。

- [x] Roughness=0、Infinite ONでIORを1にすると四面体が完全に見えなくなる。
  空気と媒体のIORが一致する直進透過の対照は、期待どおりの表示になった。
- [x] IOR=1.43、吸収色GUI表示RGB=(64,128,204)、吸収距離0.8、Infinite OFFで
  青い着色を確認。Infinite ONにすると青い着色が消え、断片状の模様は残った。
- [x] Animation停止後の観察であり、回転中だけの現象ではないことを確認。
- [x] AccumulateはONで、蓄積によって粒状ノイズが減ることをユーザーが確認。

受入範囲は材質パラメータの調整とその表示上の反応。吸収色GUIの0～255表示は、
内部の0～1の透過残存率に対応する。Infinite ONは色によらず内部吸収なしとする。
これらの確認は、全パラメータ範囲の数値精度や、設定保存、GUIの全操作を保証しない。

反射・屈折で生じる断片状の像については、この四面体と格子背景では評価が難しい。
正常とも不具合とも断定せず、反射・屈折像全体の正しさは未解決の確認事項として残す。
IOR=1の対照と吸収切替の成功を、画像全体の正しさの証明には使わない。

次は球・厚板・プリズムをそれぞれ単独Volumeとして、同条件で比較できる評価シーンを
用意する。色付き背景と反射用の明暗パネルで像を識別しやすくし、その後にIOR・
入射角・厚さ・バウンス上限を含む独立基準との照合を拡張する。

## Step 2: 観察用の球・厚板・プリズム

2026-11-01: 以下の単一Volume評価シーンを追加。Saved Scenes/Scene Editorから選択する。

- 36 `PT Volume Observation Sphere`: 球。像の湾曲・反転、輪郭付近の反射を見る。
- 37 `PT Volume Observation Slab`: 厚さ0.7の平行平板。色帯の横ずれと端面を比較する。
- 38 `PT Volume Observation Prism`: 閉じた凸三角柱のglTF。面ごとの像の方向変化を見る。

全てIOR=1.5、roughness=0、吸収距離無限、同じカメラ/露出/16バウンスで開始。
背面に色帯ターゲット、左右に反射像識別用の色帯パネルを配置する。
同じ色帯glTFを共有するが、render-preset.jsonは各シーンに分離し、Saveが他シーンへ
波及しないようにする。形状は比較のため切り替え、同時に複数Volumeを配置しない。

確認順序:
1. IOR=1で背景の色帯が連続して見えること。
2. IOR=1.5へ戻し、像のずれ/湾曲/方向変化と側面パネルの像を観察。
3. Roughnessを増やし、像のぼけを比較。
4. Infinite OFFで、同じ吸収色/距離による着色を比較。
5. 静止して蓄積を進め、ノイズと持続する模様を分ける。

再生成スクリプト: `Tests/PathTracing/volume_observation_fixture.py`。
このスクリプトは対象の3シーンと共有ターゲットを上書きするため、編集済みAssetsへ
無条件に再実行しない。生成したscene/preset/glTFはGit管理用の評価Assetsであり、
キャプチャやログとは区別する。

新規シーンのロード/配置ガードを含むCTest55/55合格。各シーン64サンプルのPNG出力は
終了コード0、D3D12エラーなし。画像を確認し、3形状の像の違いが見えることを確認した。
証拠: `bin/PathTracingValidation/volume-observation-20261101/`。
ユーザー受入、独立基準との画像全体比較、全パラメータの正しさはまだ未判定。
球は64 stacks/128 slicesの多面体近似であり、屈折像の段差を理想球の性質と混同しない。

## Step 3: 光路比較のパラメータ拡張

2026-11-01: `validate_smooth_volume_paths.py`へIOR、Y軸姿勢、一様スケール、
バウンス上限を追加。四面体の独立な倍精度CPU光路とGPUの距離/状態/出射方向を照合する。
姿勢60度は物体のY軸回転であり、面への入射角が60度であるという意味ではない。
スケールは厚さを含む全寸法を変更する。平板の厚さだけを変える試験とは区別する。

| 条件 | IOR | 姿勢 | スケール | 上限 | 打切り光路/1024 |
| --- | --- | --- | --- | --- | --- |
| matched-ior | 1 | 0 | 1 | 16 | 0 |
| water-angle60 | 1.33 | 60 | 1 | 16 | 0 |
| high-ior-half-scale | 2.4 | 0 | 0.5 | 16 | 3 |
| thick-angle60-budget4 | 1.5 | 60 | 1.5 | 4 | 54 |

全4条件・12キャプチャ合格。距離誤差は最大約5.01e-5（許容0.005）、状態/反射回数差0、
出射方向誤差は最大約6.91e-6（許容0.002）。許容値は既存のものから緩和していない。
打切りはCPU/GPUで一致し、境界見逃しや媒体不整合とは区別する。有限バウンスによる
不足を含むため、この結果を収束済みの画像品質の合格とはしない。
証拠: `bin/PathTracingValidation/volume-path-matrix-final-20261101/<条件>/report.json`。

追加CPU単体試験4件は、IOR一致時の直進、独立な幾何距離とスケール、
バウンス不足の状態、設定の不正値拒否を確認し合格。
初回の実機試験では打切り本数のNumPy整数をJSONへ直接渡してレポート出力が失敗した。
通常整数への変換を修正し、新規出力先で全件再実行済み。初回の
`volume-path-matrix-20261101`の不完全なレポートは合格扱いにしない。

今回の描画側変更はなし。Step 3全体としては、球/厚板/三角柱の独立光路や
放射輝度画像の比較、エネルギー積分、ユーザー目視評価が引き続き必要。
四面体の中央ROIの成功を、画面全体や全材質・全形状の正しさへ一般化しない。

## Step 3: 厚板と三角柱の独立光路比較

シーン37の厚板（半寸法1/0.9/0.35）とシーン38の三角柱をCPU参照へ追加。
CPUはそれぞれ6/5個の解析的な境界平面との交差を解き、GPUの三角形交差実装は使わない。
既存のIOR/姿勢/スケール/バウンス設定も形状別に指定可能とした。

各IOR=1.5、roughness=0、姿勢0度、16バウンス、同一乱数サンプルで、中央1024画素の
内部距離・状態/反射回数・出射方向を比較。2形状・6キャプチャ合格。

| 形状 | 距離最大誤差 | 状態/反射回数差 | 方向最大誤差 | バウンス打切り |
| --- | --- | --- | --- | --- |
| 厚板 | 1.17e-6 | 0 | 2.30e-6 | 0/1024 |
| 三角柱 | 1.12e-6 | 0 | 2.30e-6 | 0/1024 |

許容値は距離0.005、状態0、方向0.002のまま。CPU単体試験は、IOR一致時の厚板の
解析的距離と三角柱の軸方向距離を追加し、既存試験を含む6/6合格。
証拠: `bin/PathTracingValidation/volume-shape-paths-20261101/{slab,prism}/report.json`。

描画コードや材質モデルの修正はなし。球は64 stacks/128 slicesの実メッシュと
理想球の差を扱う必要があるため、独立な光路比較は未実施。
今回も中央ROI/固定条件の光路試験であり、画面全体の放射輝度やエネルギー保存、
粗い境界、全姿勢・全材質の受入には一般化しない。

## Step 3: 球の実メッシュとの光路比較

`SceneBuilderMeshTests --export-sphere <path>`で、実際のC++球生成関数から
半径1、64 stacks/128 slicesの8,385頂点・16,384三角形を書き出す診断オプションを追加。
スナップショットは生成証拠としてbinに置き、Gitには含めない。
CPUはこの頂点/インデックスを使うが、三角形交差は独立した倍精度実装を使う。
理想球の解析的交差との比較ではなく、シーン36の実メッシュに対する比較である。
同一頂点データの共有は意図的で、GPUと独立な形状生成まで保証する試験ではない。

IOR=1/1.5、roughness=0、姿勢0度、16バウンス、各中央1024光路・3キャプチャ合格。

| IOR | 距離最大誤差 | 状態/反射回数差 | 方向最大誤差 | 上限打切り |
| --- | --- | --- | --- | --- |
| 1 | 3.00e-6 | 0 | 2.30e-6 | 0/1024 |
| 1.5 | 0.001096 | 0 | 0.000504 | 0/1024 |

既存許容値（距離0.005、状態0、方向0.002）は変更していない。
球の誤差は平面形状より大きく、完全一致や全入射角の精度保証とはしない。
三角形交差/前面の除外/ミスの単体試験を追加し、Python8/8合格、CTest55/55合格。
証拠: `bin/PathTracingValidation/sphere-path-reference-20261101/{snell,matched}/report.json`。
各レポートは使用メッシュのSHA256も記録する。描画コードの変更はなし。

これで球・厚板・プリズムの代表条件で光路を照合できた。画面全体の放射輝度、
粗い境界のエネルギー評価、動作中の品質、ユーザーの目視受入は別途必要。

## Boundary Energy Accounting

The rough-volume GPU contract test now reports 28 cases of eta-normalized
boundary throughput, macro-hemisphere rejection, and Smith masking loss.
Transmission radiance weights are divided by (incidentIOR / transmittedIOR)^2
before accounting; raw radiance throughput is not bounded by one on exit.
Rejected events remain zero contributions and are not renormalized away.
Each normalized sample must be in [0, 1] within 0.0002, and throughput plus
rejection and masking loss must sum to one within 0.0002.
Smooth and matched-IOR boundaries must have unit normalized throughput.

This is deterministic accounting of the existing 1024 samples per case, not
an independent hemispherical BSDF integral, a confidence interval, or a closed
Volume white-furnace acceptance test. Single-scattering masking/rejection loss
must not automatically be classified as an integration bug or compensated by
renormalizing the sample weights. Whole-volume energy qualification remains open.

2026-11-04: Debug CMake build passed. The WARP rough-volume test passed all
28 cases / 28,672 samples with no D3D12 errors. Smooth and matched-IOR cases
returned unit normalized throughput. At roughness=1, viewZ=0.1, air-to-glass,
normalized throughput was 0.400539, rejection 0.0107422, masking loss 0.588719.
These are finite deterministic-sample measurements, not converged integrals.
Evidence: bin/PathTracingValidation/thin-gltf-build-20261024/Testing/Temporary/LastTest.log
(generated, not tracked; later CTest runs may overwrite it).

## User Observation Acceptance (2026-11-04)

- Scene 36 sphere: curved color bands confirmed at IOR=1.5. Increasing
  roughness from 0 to 0.2 and 0.5 progressively blurred the bands without
  observed black gaps or abrupt brightness changes.
- Scene 36 absorption: color (64, 128, 204), finite distances 1.0/0.5/0.2,
  and Infinite switching were confirmed OK by the user.
- Scene 37 slab: the requested matched-IOR continuity, refracted band offset,
  and roughness sequence were confirmed OK by the user.
- Scene 38 prism: the requested matched-IOR continuity, face-dependent band
  displacement, and roughness sequence were confirmed without problems.

The scene 36 matched-IOR disappearance was requested, but not separately
reported in this session; do not treat the curved-band screenshot as proof
of that control. Numerical matched-IOR path coverage is recorded above.
This closes the requested basic visual walkthrough, not whole-image physical
accuracy, rough-volume energy qualification, or all glTF material support.

Regression: 52/55 CTest cases passed in the restricted run; the three
file-save tests (EvaluationState, SceneDocument, RenderPresetStore) passed
on rerun outside the filesystem restriction. No test thresholds were changed.

## Independent Outgoing-Direction Integral (2026-11-05)

Added double-precision midpoint integration over outgoing solid angle to the
rough-boundary test. It does not call the GPU evaluator, VNDF sampler, or its
PDF; it directly integrates the single-scattering dielectric BSDF times cosine.
The physical BSDF equations are intentionally shared in meaning, so this is
an independent integration path, not an independent material model.
Transmission is eta-normalized as in the previous accounting test.

Coverage: air-to-glass IOR 1.5, roughness 0.7/1.0, viewZ 1/0.8/0.6/0.1
(eight cases). Integrals use uniform cosine/azimuth grids of 512x1024 and
1024x2048 points per hemisphere. Required refinement difference <=0.003;
GPU 1024-sample deterministic mean difference <=0.03. The latter is a finite
sample regression allowance, not a statistical confidence interval or proof
of unbiasedness. The unit-energy upper allowance is 0.003 for quadrature.

Debug test target built successfully. WARP test passed all eight integral
comparisons and the existing 28,672 sample contracts, with no D3D12 errors.
Largest refinement difference was about 0.001184; largest GPU difference
was about 0.003294. The initial 128/256 grids failed convergence; resolution
was increased without changing the acceptance thresholds. No rendering code
or sample-weight compensation was changed.

This verifies selected broad lobes only. Low roughness, glass-to-air/TIR,
all IORs, whole-Volume white furnace, and multiple-scattering compensation
remain outside this acceptance. Next: whole-Volume furnace controls and
bounce-budget convergence, distinguishing boundary-model loss from truncation.

## Whole-Volume Furnace Controls (2026-11-05)

Added validate_volume_furnace.py and four assessment unit tests. The runtime
fixture contains only one white, nonabsorbing slab, with unit constant
environment (no environment-map asset), no direct light, no Russian roulette,
native reconstruction and Radiance output. Existing capture contracts now
compare environmentEnabled against the requested preset instead of hardcoding
false; existing environment-disabled fixtures retain the same requirement.

Six cases captured at 16 accumulated samples, central 32x32 ROI:

| IOR | Roughness | Bounces | RGB mean (same in each channel) |
| --- | --- | --- | --- |
| 1 | 0 | 4 | 1.0 |
| 1 | 0.7 | 4 | 1.0 |
| 1.5 | 0 | 4 | 0.998673 |
| 1.5 | 0 | 16 | 1.0 |
| 1.5 | 0.7 | 4 | 0.704255 |
| 1.5 | 0.7 | 16 | 0.714193 |

All six captures completed without D3D12 errors; assessment tests passed 4/4.
Evidence: bin/PathTracingValidation/volume-furnace-radiance-20261105/report.json.
Matched-IOR controls require unit radiance within 0.002. All ROI values must
be finite and in [-0.0001,1.002]. Rough cases are measurements, not accepted
against a unit-radiance target. The 16-sample result does not establish
statistical or bounce-budget convergence; more samples and intermediate
budgets plus status diagnostics are needed before assigning all residual loss
to the boundary model. Whole-Volume qualification remains open.

The initial volume-furnace-20261105 run accidentally used Albedo (output 0),
not Radiance (3). Its unit values are invalid as furnace evidence, regardless
of that initial report's status. All six were rerun with Radiance into the
separate evidence folder above. No renderer fix or weight compensation was made.

## Furnace Budget Sweep (2026-11-05)

Extended the runner to budgets 4/8/16, explicit optional window dimensions,
and complete-case budget summaries (missing/duplicate/nonfinite means rejected).
Assessment unit tests: 5/5 passed. No rendering changes.
Eight runtime cases passed at 256x256, 128 accumulated samples, with no D3D12
errors. Window dimensions are checked against the captured image. The central
32x32 ROI spans a different angular area than the earlier 1920x1080 captures;
do not interpret differences across those runs as sample convergence.

| Boundary | Budget 4 | Budget 8 | Budget 16 |
| --- | --- | --- | --- |
| Smooth, IOR 1.5 | 0.998604 | 1.0 | 1.0 |
| Roughness 0.7, IOR 1.5 | 0.703604 | 0.712524 | 0.712582 |

Both matched-IOR controls remained 1.0. Values are linear RGB means (all
channels equal). Rough 8-to-16 improvement is about 0.0000577 versus residual
from unit radiance about 0.287418. This supports a small finite-budget effect
between these two budgets in this fixture, not a proof that all remaining
loss is correct single-scattering masking. Rejected/budget/error path counts
and a second sample budget are still needed to qualify that explanation.
No confidence interval, all-angle convergence, or full white-furnace acceptance
is claimed. Existing pass status deliberately says controls passed and finite
budgets measured, not rough energy conservation passed.

Evidence: bin/PathTracingValidation/volume-furnace-budgets-20261105/report.json
(includes budgetComparison, per-case runtime/metadata, inputs and buffer hashes).

## Furnace Status Assessment Preparation

Added a separate single-path Volume Status assessor for the same central
32x32 ROI. It counts normal (0), budget termination (3), and rough hemisphere
rejection (7), rejecting all other status codes and nonfinite evidence.
Two synthetic status tests plus the five furnace tests pass (7/7).
This assessor must receive CurrentFrameRadiance with debugOutput=5, not an
accumulated or denoised status image. It has not yet been connected to a
new runtime diagnostic sweep, so no measured path counts or root-cause
conclusion are claimed. Existing furnace evidence remains radiance-only.

## Sparse Runtime Status Probes (2026-11-06)

Connected the status assessor through validate_volume_furnace_status.py.
The runner derives inputs from the completed furnace sweep, changes only
debugOutput to 5, and captures CurrentFrameRadiance at sample indices 1/17
for roughness 0.7, budgets 8/16, 256x256. Each ROI has 1024 paths.
All four captures completed, without medium/boundary errors or D3D12 errors.
At budget 8 the normal/rejected counts were 790/234 and 783/241, respectively;
budget termination was zero at both indices. Budget 16 produced the same
counts at both indices. No accumulated status image was used.

Evidence: bin/PathTracingValidation/volume-furnace-status-20261106/report.json.
These sparse probes support rough-boundary rejection as a contributor and
show no truncation in the observed paths. They do not measure all 128 sample
indices, explain the entire radiance deficit, or exclude errors elsewhere.
Smith weighting also loses energy without setting rejection status.
No rendering change or ad hoc renormalization was introduced.

## Second Sample Budget (2026-11-06)

Repeated all eight furnace cases at 256 samples with unchanged 256x256 inputs.
All runtime contracts passed with no D3D12 errors. Added comparison tooling
that checks binary/shader hashes, case completeness, input hashes and ROI;
three comparison unit tests passed. Results are deterministic nested samples,
not independent statistical trials or confidence intervals.

Roughness 0.7 means at budgets 4/8/16 were 0.703597/0.712548/0.712613.
The largest absolute 128-to-256 mean change across all cases was 0.0000310.
Smooth budgets 8/16 and both matched-IOR controls remained 1.0.
The persistent deficit is not explained by the observed finite sample or
8-to-16 budget change. Sparse status probes and the single-boundary integral
support single-scattering model loss as a contributor, but do not independently
prove the entire closed-volume estimator correct. No weight compensation added.

Evidence: bin/PathTracingValidation/volume-furnace-256spp-20261106/report.json
and comparison.json. Next decision: retain/document the single-scattering
quality limit or implement a separately validated microfacet multiple-scattering
model. Do not normalize surviving samples to unit energy: that would change
the BSDF and potentially bias reflection/transmission balance.

## Decision: Retain Single-Scattering Quality Limit

User approved recording this limitation and continuing to glTF material work.
The rough Volume energy deficit is retained as a known quality limitation,
not declared a fully proven absence of integration bugs. Multiple-scattering
transport is a separate future theme. Existing tests and evidence remain as
regression controls; no normalization, relaxed thresholds or renderer changes
are introduced to hide the deficit. Whole-volume physical qualification stays
open while the current representative visual acceptance remains valid.

## Next Material Expansion: Inventory

Code inspection of GltfLoader.cpp confirms that active solid Volume requires
factor=1, IOR<=4, metallic=0, white baseColorFactor, OPAQUE coverage, no sampled
base color/transmission/metallic-roughness textures, and no emission. Scalar
roughness is allowed. Normal textures remain excluded for active transmission.
These restrictions are not a statement that all corresponding thin-sheet
features are missing; thin transmission already has separate texture support.

Next implementation target: metallicRoughnessTexture G-channel roughness for
solid Volume, reusing the existing texture sampling path while retaining
dielectric metallic=0 and geometric boundary normals. Before lifting the
loader guard, verify sampled B-channel metallic cannot silently activate a
metallic Volume. Add accepted/rejected loader fixtures, factor-times-texture
tests, and runtime smooth/rough spatial comparison. Do not enable partial
transmission, tinted boundary base color, thickness textures, normal maps or
additional media in the same change. This entry is a scoped plan, not a claim
that Volume textures are already enabled.

## Volume Roughness Texture: Initial Loader Change

Removed only the metallicRoughnessTexture exclusion from the solid material
guard. Metallic factor must remain zero; shader SceneRayQuery.hlsli already
uses G*roughnessFactor and B*metallicFactor, preserving dielectric behavior.
Existing TEXCOORD_0, texture-info and sampled-image validation remain active.
No shader change. Loader regression passed after Debug build (GltfNodeMesh).
Tests check textured material reaches the separate VolumeMesh shape guard,
and reject nonzero metallic factor and TEXCOORD_1. The thin fixture is not a
closed convex solid, so it cannot prove successful solid mesh conversion.
An initial test incorrectly expected that fixture to load; corrected to retain
the geometry guard. A valid textured convex fixture, material-index retention,
factor/channel GPU comparisons and visual acceptance are still required.
Do not treat this loader change alone as completed Volume texture support.

## Valid Convex Textured Fixture

Extended the closed tetrahedron loader fixture with four UV coordinates and
an embedded PNG from the existing texture fixture. Successful asset loading
and SceneBuilder conversion preserve Volume/imported-Volume identity,
metallicRoughness texture index, roughnessFactor=0.7 and metallicFactor=0.
Removing TEXCOORD_0 is rejected. Debug build and GltfNodeMesh regression pass.
Fixture files are created temporarily by the C++ test and cleaned up; the
test source is tracked. This establishes valid convex texture loading, not
GPU factor/channel sampling accuracy or visual acceptance. Next: patterned
G-channel inputs and scalar controls through Native PT radiance captures.

## GPU Comparison Fixture Preparation

Added volume_roughness_texture_fixture.py: derives the closed tetrahedron
from scene 35, removes turntable/backgrounds, appends UVs to its embedded
buffer, and emits a constant RGBA=(32,128,255,0) texture with factor=0.7.
The paired scalar fixture uses roughness=0.7*128/255. Both use metallic=0.
Output is VolumeOutgoingDirection (6) for a deterministic path comparison;
this initial pair is constant-texture, not yet spatial-pattern acceptance.
Generation and buffer/UV/scalar-control checks passed. Captures have not yet
been executed with a rebuilt application; no GPU equality claim is made.

## Constant Texture GPU Comparison

Rebuilt Debug application successfully (0 errors, 4 warnings). Added
validate_volume_roughness_texture.py with three deterministic captures:
texture G=128 times factor 0.7, equivalent scalar 0.7*128/255, and smooth
scalar zero. Compares finite full images within 0.002, requires nonzero
direction coverage, and requires a difference from the smooth control.
Two assessment unit tests passed. Runtime report completed successfully,
with no D3D12 errors. This validates a constant texture/channel-factor case,
not spatial interpolation, seams, all UVs or a full-image radiance result.
Evidence: bin/PathTracingValidation/volume-roughness-texture-gpu-20261106/report.json.
Next: patterned G texture and UV-dependent spatial controls before declaring
Volume roughness-texture support complete.

## Patterned G and UV Probes (2026-11-07)

Extended the fixture to a two-region G pattern (16/240), alternate R/B/A
channels, and horizontally mirrored UVs. Three Native PT captures completed
without D3D12 errors. Keeping G unchanged while changing R/B/A produced
exactly identical outgoing-direction images (maximum difference 0).
Mirroring UVs changed 11,295 pixels above a 0.01 direction-image threshold;
minimum required coverage was 128 pixels.
Evidence: bin/PathTracingValidation/volume-roughness-pattern-20261107/report.json.

Together with the constant-G/scalar comparison, these probes establish basic
G-channel/factor use and UV-dependent paths for the selected convex fixture.
They are not an independent per-pixel interpolation reference or validation
of seams, all UVs, normal maps, other Volume textures, or full radiance quality.
Next: a saved visual review scene and user acceptance of patterned roughness,
then consolidate the scoped Volume roughness texture change for PR.

## Saved Visual Review Scene 39

Added 39-volume-roughness-texture-review with a UV-mapped convex tetrahedron,
two-region G roughness texture and shared color-band targets from scene 36.
Independent render preset starts in accumulated Native PT Radiance at factor
0.7, IOR 1.5, no absorption. Material Controls roughness zero is the smooth
control; restore 0.7 for patterned roughness. No turntable.
Generator refuses to overwrite an existing directory. glTF infinite attenuation
is represented by omitting attenuationDistance, not JSON null.
512x512/16-sample smoke capture exited 0, completed PNG, no D3D12 errors;
image inspected for visible geometry and backgrounds. Low-sample image remains
noisy and is not visual acceptance. User walkthrough remains pending.
Evidence: bin/PathTracingValidation/scene39-review.png and scene39-review.log.

User visual acceptance (2026-11-07): scene 39 roughness sequence
0.7 -> 0.0 -> 0.7 completed without issues. This covers the visual roughness
control only; Evaluation Case ROI/camera restore is a separate check.

## Thin Transmission RR Admission Baseline (2026-11-08)

Scene 20, fixed camera, 768x432: Native reference captures at 1 and 128 spp
completed with no D3D12 errors. Evidence: bin/PathTracingValidation/
scene20-rr-baseline-{1,128}.png and matching logs.

On the PR 121 Debug build, an explicit RR request with native opt-in
completed the 16-frame capture but reported requested=rr, output=native,
status=incompatible-inputs, nativeEvaluateCallCount=0, outputVerified=false.
Evidence: scene20-rr-admission-20261108.png and matching log in the same folder.
This is a fallback/admission check only. Do not score it as RR quality or
remove the guard merely to obtain a capture. When the RR transmission path
lands, verify actual RR output and compare glass interior, silhouette bands
and outside-glass controls at the same camera, exposure and scene settings.

### Fixed Image Regions for Scene 20

For the existing 768x432 captures only, use pixel rectangles [x0,y0,x1,y1)
below. Keep the window size and camera unchanged; do not reuse these pixel
coordinates after changing framing.

| Region | Rectangle | Purpose |
| --- | --- | --- |
| Open control | [137,132,239,300) | Background grid and orange object edges |
| Clear pane interior | [268,130,369,301) | Native blur, color and residual detail |
| Blue pane interior | [400,130,499,301) | Transmission tint and residual detail |
| Opaque pane interior | [531,130,629,301) | Occlusion control; no recovered background |
| Clear pane left edge | [248,133,269,300) | Frame silhouette and edge bleed |

The current scene materials use roughness 0.5. At 128 spp the clear/blue pane
interiors already blur the background strongly. Do not require RR to restore
detail absent in the Native reference. For a sharp-detail retention test, use
a separate smooth control with identical framing and regenerate both Native
and RR captures for that control. PNG comparisons are display-space only;
use linear SceneColor evidence for quantitative transport measurements.

### Smooth Thin Native Control

Added Tests/PathTracing/thin_rr_smooth_fixture.py to generate a non-overwriting
derivative of scene 20. Only the two active thin pane roughness values become
zero; the camera, geometry, opaque control, tint and render preset stay fixed.
Captured Native 1 and 128 spp at 768x432 with zero D3D12 errors. At 128 spp the
grid and orange-object silhouettes are visible through both active panes,
while the opaque control still hides them. The fixed ROI coordinates apply.
Evidence: bin/PathTracingValidation/scene20-smooth-rr-control/native-{1,128}.png
and matching logs. RR quality is not evaluated by these captures.

### Smooth Solid Native Control (Scene 37)

Captured the unmodified slab preset at 768x432, Native 1/128 spp, max bounces
16. Both captures completed without D3D12 errors. At 128 spp the color bands
shift laterally inside the slab; its right side shows a different band pattern.
Compare the refracted interior [312,151,448,278), right boundary/side
[448,150,489,278), and rear target above the slab [312,47,448,123).
Rectangles use [x0,y0,x1,y1) pixels for this framing only. The outside target
is a control, not the desired image inside the slab. Compare RR to Native at
the same location; do not interpret the expected shift as reconstruction error.
Evidence: bin/PathTracingValidation/scene37-rr-native-{1,128}.png and logs.
RR output has not been evaluated for this scene.

### Rough Solid Native Control (Scene 33)

Captured the unmodified rough cube preset at 768x432, Native 1/128 spp,
16 bounces, with no D3D12 errors. The blue absorption tint and broad blur
across the checker edges are present in the 128 spp Native baseline. The
exterior checker and glass silhouette remain comparatively sharp.
Compare glass interior [278,127,483,294), right silhouette and side
[488,123,542,301), and exterior checker edge [550,166,670,222).
Rectangles are [x0,y0,x1,y1) for this framing. Check tint, blur width and
edge bleed; do not demand unblurred checker detail inside the rough glass.
Evidence: bin/PathTracingValidation/scene33-rr-native-{1,128}.png and logs.
RR output has not been evaluated for this scene.

### Isolated Thin Motion Controls

Added Tests/PathTracing/thin_rr_motion_fixtures.py. It generates three
matched-framing derivatives of the smooth scene 20 control: camera orbit
only (3 deg/s, no animated node), Clear Pane translation only, and Sphere 2
behind the Clear Pane translation only. Both object tracks use X amplitude
0.2 and period 4 s. Generated motion-plan.json specifies 768x432 and 30 fps.
Generation succeeded and both animation targets passed root/visibility/type
checks. No tracked scene asset was modified.

Use fixed-step and matching start poses/frame indices. Camera-only uses
Capture Session orbit; object-only uses the scene animation (Space to play),
with camera orbit disabled. Evaluate silhouette, background through the
pane, disocclusion and recovery after stopping separately. Do not use the
opaque RR lifecycle runner as proof of transmission RR output. Motion
captures and backend comparisons are not yet run.

### Camera-Only Native Motion Smoke (2026-11-09)

Ran the camera-only control with Native PT, 768x432, fixed-step 30 fps,
3 degrees/sec orbit, 16 PNGs. All 16 saved, zero dropped, exit 0, no D3D12
errors. Camera records match indices 0-15 and t=index/30; at t=0.5 s the
camera has orbited 1.5 degrees. No scene node animation is present.
Evidence: bin/PathTracingValidation/thin-rr-motion-controls-20261108/
camera-native/ and camera-native.log. This is a low-sample timeline smoke,
not a converged moving reference or an RR quality result. The glass-only
and behind-glass object motion captures remain pending.

### Object-Only Native Pose Smoke (2026-11-10)

Added --pose-seconds to the motion fixture generator for static snapshots
of the existing translation track. Compared t=0 and t=1 s (X offset 0.2)
for Clear Pane and Sphere 2 separately. Scene JSON comparison confirmed the
camera was identical and only the designated node changed. Four Native
768x432/16 spp captures completed with no D3D12 errors.

At an 8/255 maximum-channel display difference threshold, the clear-pane
region [250,105,386,327) changed in 1042 pixels for glass motion and 2237
for sphere motion. The open control ROI changed in 0 and 33 pixels,
respectively; this is not an assertion of zero indirect image differences.
Evidence: bin/PathTracingValidation/thin-rr-static-pose-{0,1}-20261110/.
These are independent stills, not continuous motion, history or RR results.

### Upstream RR Handoff (2026-11-10)

Origin main 30c083a (PR 123) delivers opt-in thin, smooth nonabsorbing
Volume, and rough nonabsorbing Volume RR (tested roughness 0.35). Its
doc/rr-evaluation-todo.md records verified continuous smooth slab and
behind-thin-glass object motion with the additional motion experiment flag.
The latter was visually accepted with slight smearing; the guide still
tracks the primary surface, not the image behind it. Absorbing Volume and
ordinary RR selection remain Native fallback by design. Scene 33's original
absorbing material is therefore a Native baseline, not an admitted RR case.

This branch's baseline and motion fixtures are supplemental controls; do
not repeat upstream admission work or claim a local RR rerun. A trial merge
found conflicts due to the pre-squash PR 121 history. No merge or source
change was made. For a subsequent local RR comparison use a branch based
on current origin/main and carry only the post-PR-121 evaluation material.
