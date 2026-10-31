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
- [ ] 薄膜、MASK、不透明、滑らかなVolumeの既存回帰を基準を緩めず再実行。

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
