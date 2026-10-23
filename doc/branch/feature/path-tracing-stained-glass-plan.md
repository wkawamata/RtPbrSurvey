# 色付き薄膜透過の1日実装プラン

作成日: 2026-10-23
状態: 保存契約とNative PT薄膜描画を実装・自動検証済み。GUI・目視評価は未着手。
対象workspace: `C:\work\RtPbrSurvey-work-3`
想定作業量: 実働8時間。テストシーン作成、ビルド、検証、修正を含む。
実装ブランチ案: `codex/pt-thin-transmission`

## 最終目標

Scene Editorでステンドグラスを配置し、その色・模様を通過した光が
床や壁を照らす様子をNative Path Tracingで再現する。
将来はガラスの厚み、屈折率、吸収による見え方の違いにも対応する。

「カメラからガラス越しに背景が見えること」と「光源からガラスを
通った光が床に届くこと」は別の実装・評価項目とする。
1日版で完成させるのは前者の土台であり、後者の完成は主張しない。

## 1日版の到達点

- Scene EditorでPlaneを追加し、既存Transformで移動・回転・拡縮できる。
- 材質に明示的な `Opaque / Thin Transmission` モードを設ける。
- Thin Transmissionは滑らかな厚みゼロの板とし、背景の歪みは付けない。
- 色付き直進透過とFresnel表面反射をNative PTで表示できる。
- GUIでRGBAとIORを変更し、即時反映・PT蓄積リセットできる。
- シーンを保存・復元し、Saved Scenesから通常実行できる。
- 3種類の小さな検証シーンと、日本語の説明・確認項目を保存する。
- 不透明シーンの既存動作を維持する。

リアルタイムに操作可能な初期機能を目指すが、固定解像度での60fpsや
低ノイズを保証する納期ではない。対応範囲を限定した実験的導入とする。

## RGBAと材質契約

| GUI項目 | 意味 | 保存・内部表現 |
| --- | --- | --- |
| RGB | 透過光に掛ける一定の色 | `transmissionTint`。演算はlinear RGB |
| A | 不透明度。1=不透明、0=最大透過 | `transmissionFactor = 1 - A` |
| IOR | 表面反射に使う屈折率。既定1.5 | `ior`。将来は体積境界の屈折にも使用 |
| モード | 不透明／薄膜の明示的な区別 | `Opaque / ThinTransmission` |

色UIの表示空間・保存空間の変換は既存規約に合わせ、二重ガンマ変換をしない。
RGBAは編集のための表現であり、内部データを単一RGBA値だけに限定しない。
glTFのbaseColor alphaを独自解釈して透過量に転用しない。

A=0でも表面反射は残る。IOR=1かつ白色・最大透過の薄膜は、
検証条件下で背景を変えないことを確認する。
A=1では、同じ不透明材質へのフォールバックと一致する契約にする。
Opaqueモードは既存シェーダーの結果を維持する。

1日版の着色は薄膜を通るたびに一定色を掛ける近似であり、
厚みに応じたBeer-Lambert吸収ではない。反射／透過／非透過成分の
エネルギーを二重計上せず、確率選択に対応するthroughput/PDFを使う。
滑らかな透過を既存の粗いBRDFのPDFやNEE/MIS状態と混同しない。

## 実装の境界

### 材質とglTF

SceneDocument、CPU材質、GPU材質の契約を揃え、既存ファイルで項目が
省略されていればOpaque・透過量0を維持する。CPU/HLSLのレイアウトを検証する。
Scene EditorのRGBAは透過専用項目とし、既存baseColorや発光の意味を変えない。

glTFは `KHR_materials_transmission` の定数factorと
`KHR_materials_ior` の通常IORを読み込む限定対応を目標にする。
透過色はglTFのbaseColorとの対応を仕様に照合し、独自の薄膜着色と
同一とみなせない条件は警告する。全拡張対応としては宣言しない。
透過テクスチャ、粗い透過、volume、特殊IOR、未対応のrequired拡張は
従来の厳格な診断を維持し、未実装なのに対応済みとして受理しない。

### Ray Queryと影

カメラ／二次レイが薄膜に当たった際に反射か直進透過を選ぶ。
裏面から見た薄膜も検証し、必要なカリング変更はPTの対象経路に限定する。
既存BLASのopaqueフラグと、材質上の光学的透過は別物として扱う。
初日はalpha候補棄却や共通Ray Query全体の書き換えを行わない。
透過イベントも上限を消費し、重なった板での無限継続を防ぐ。
原点オフセットは透過の進行側へ適用し、同一面への再衝突を検証する。

影レイの色付き透過は初日の対象外。直接光の影は板を不透明遮蔽物として
扱う制約を表示する。二次経路で偶然得られる透過照明があっても、
色付き投影が完成した、あるいは収束性能を確認したとは判定しない。

### NRDと他の描画経路

透過材質が有効なシーンでは、保守的にシーン全体をNative PTへ
フォールバックさせ、Debugに理由を表示する。NRDへ不正な入力を流さない。
材質変更で透過が有効になった場合も同じガードを適用する。
Hybrid / Deferred、DLSS RRの透過対応は行わず、PT限定であることを明示する。

## 8時間の作業割当

| 順序 | 目安 | 作業 | 確認点 |
| --- | ---: | --- | --- |
| 0 | 0.5h | 契約確定、Planeの表裏・材質経路を調査 | 対応／未対応を固定 |
| 1 | 2.0h | SceneDocument、CPU/GPU材質、定数glTF読込み | 既定値・レイアウト・読込み診断 |
| 2 | 2.0h | 滑らかな薄膜の反射／直進透過 | 確率重み・表裏・自己交差・MIS状態 |
| 3 | 1.0h | Scene EditorのRGBA・IOR、蓄積リセット、NRDガード | 即時反映・保存復元 |
| 4 | 1.5h | 3シーン作成、自動テスト、ビルド・Debug Layer確認 | 下記の必須テスト |
| 5 | 1.0h | ユーザー目視確認、修正予備、制約の記録 | 未確認を合格扱いしない |

8時間は見積もりであり確約ではない。確率重みや交差判定に問題が残れば
GUI装飾・外部glTFの追加サンプルを削り、正しさの修正へ時間を回す。
必須条件が未達なら「部分完了」として残し、検証や制約表示を省略しない。
屈折・色付き影の実装を途中追加して日程を延ばさない。

## Git管理するテストシーン

保存先は次を予定する。実装開始時に番号の競合を再確認する。
各フォルダに `scene.json`、`render-preset.json`、必要な小規模アセットと
READMEを置き、sceneのdescriptionにも日本語で目的・操作・判定を記載する。

| フォルダ案 | 内容 | 観察すること |
| --- | --- | --- |
| `17-thin-transmission-baseline` | 白／赤／緑／青の板、不透明比較物、識別しやすい背景 | 色、透過量、背景の可視性 |
| `18-thin-transmission-angle` | 角度違いの板と一定輝度の環境 | 斜め方向の反射、裏面からの表示 |
| `19-thin-transmission-layers` | 重なった板、間に置いた不透明物 | 色の重なり、遮蔽、自己交差 |

初日の板はPlaneを使い、厚いBoxを薄膜の代用として使わない。
透過境界の回数に足りるバウンス数をpresetに明記し、上限による黒化を
透過不具合と混同しない。生成ログ・比較画像はbin配下に置き、コミットしない。

## 必須テストと完了条件

- [ ] 既存JSONの既定値、追加項目のround-trip、非有限値・範囲外の拒否。
- [ ] Opaqueの既存結果を維持し、A=1の非透過端点を検証。
- [ ] 白・A=0・IOR=1で、十分なバウンス数における背景一致を検証。
- [ ] IOR=1.5の垂直入射Fresnel反射率が約4%となる数式テスト。
- [ ] 透過色による意図したRGB変化、反射／透過の期待値を検証。
- [ ] 表裏、複数枚、遮蔽物、有限性、自己交差、バウンス上限を確認。
- [ ] 透過有効時はNRDがNativeへフォールバックし、理由が表示される。
- [ ] RGBA／IOR／Transform変更が即時反映され、PT蓄積がリセットされる。
- [ ] Save→再読込み→Saved ScenesのRunで設定を再現する。
- [ ] Debug x64ビルド・関連テスト成功、対象実行でD3D12 ERRORなし。
- [ ] ユーザーが静止・移動後の表示を確認する。

確率的な比較は固定seedの複数試行と不確かさを使う。
許容差はテスト前に決め、後から合格するよう緩和しない。
1 sppの画像一致や、見た目だけでエネルギー保存を判定しない。
既存の長時間NRD収束研究は再開しない。

コミット案: (1)材質契約・読込みとテスト、(2)PT薄膜・ガード、
(3)エディタ・シーン・評価記録。小さく分け、mainへ直接実装しない。
現在のシーン10のユーザー編集は今回の変更へ混ぜず保持する。

## 最終目標までの後続ステップ

1. 色付き薄膜: 今回の1日版。配置・RGBA・反射・直進透過。
2. ステンドグラスの模様: 透過色／透過量テクスチャ、不透明な鉛枠。
3. 色付き透過照明: shadow/transmittance queryで経路上の板を順に評価し、
   直接光・面光源NEEの可視性へRGB透過率を反映する。
   枠の不透明遮蔽、複数枚、光源の手前／奥、距離と方向を検証する。
   ラスタの色付きオーバーレイではなく、照明の光路に組み込む。
4. 最終目標の評価: 色付き窓＋不透明な枠＋白い床／壁＋光源を保存し、
   透過OFF／無色／RGB模様／窓移動／光源移動で投影が正しく変わることを
   確認する。限定条件の解析値または独立参照とも比較する。
5. 厚み・屈折・吸収: volume境界、IORによる入出射、全反射、内部距離に
   応じた吸収を追加する。単独の閉じた形状から始め、媒体の重なりは後回し。
6. 高度な光輸送とデノイズ: 厚いガラスで曲がった光が作るコースティクスは
   直線shadow queryでは解決しないため別設計。最後にNRD／RRの入力・合成を検証。

将来のvolumeは別の材質モードとする。IOR、透過率、吸収色、吸収距離を
別プロパティにし、薄膜RGBを自動的に体積吸収へ読み替えない。
初日は未使用の体積パラメータや媒体スタックを大量に追加せず、スキーマの
後方互換性を保った追加で対応する。1日版で最終目標の完了を主張しない。

## 参照仕様

- [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
- [KHR_materials_ior](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_ior)
- [KHR_materials_volume](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume)

alpha coverage、薄膜の透過、体積の屈折・吸収を区別するための参照。
これらの全仕様対応を1日の完了条件にはしない。

## 実装進捗

- [x] 設計MDをコミット。実装ブランチ `codex/pt-thin-transmission` を作成。
- [x] SceneDocumentに独立したmode・factor・tint・iorを追加。
- [x] 旧JSONの不透明な既定値と、既定材質の既存出力形式を維持。
- [x] JSONの追加項目は `transmission` オブジェクトとし、不正値を拒否。
- [x] Debug x64でSceneDocument／SceneDocumentBuilderをビルドし、関連CTest 2/2合格。
- [x] CPU/GPU材質への伝搬、Native PTの薄膜反射・直進透過。
- [ ] 定数glTF透過拡張の読込み。既存の未対応診断を維持している。
- [x] 透過シーンでNRDを使わないガードとNative fallback表示。
- [ ] Scene EditorのRGBA・IOR、Git管理の検証用3シーン。

最初の作業単位では保存契約だけを追加し、未接続のThin材質を不透明として
黙って表示しないためのBuilderガードを置いた。2番目の作業単位で描画を
接続し、この一時ガードを除去した。glTF対応済みの宣言はまだ変更しない。

確認内容: 旧JSONの既定値、透過JSONのround-trip、実ファイルのatomic saveと
再読込み、透過量の両端・IOR=1、未知のモード／項目・型違い・欠落・範囲外・
非有限値の拒否、失敗時のdocument／公開済みscene保持。
上記の保存契約のみの段階では、透過のGPU描画・目視評価は行っていなかった。

### Native PT接続の確認、2026-10-24

- CPU材質からGPU Materialへfactor・tint・IORとThinフラグを伝搬。
  structured bufferは112 bytes。CPUのサイズ／各offsetをstatic_assertで固定。
- Thinの候補だけ裏面も受理。不透明シーンでは従来のカリングを維持。
- 既存の不透明成分とThin成分を混合し、ThinはFresnel反射／色付き直進透過を
  選択する。透過量0では追加の乱数を消費しない。
- delta経路を連続BSDFのMISに混ぜない。カメラから直進透過だけを経た背景は
  primary backgroundの扱いを維持し、IOR=1の白い板による背景変化を防ぐ。
- 透過が有効な材質を持つシーンは保守的にNRD変換・評価・合成を実行しない。
  Native fallbackの理由と、影は不透明・体積屈折なしの制約をDebugに表示する。
- Fresnel式の参照: [PBRT scattering](https://github.com/mmp/pbrt-v4/blob/master/src/pbrt/util/scattering.h)。
  今回は単一の有効表面の近似であり、有限厚の平行ガラス内部の多重反射モデルではない。

検証結果:

- SDK有効Debug x64の全ビルド成功。途中の並列Assetsコピー競合は、直列ビルドで解消。
- CTest 40/40、既存NRD Pythonテスト197件、新しい比較テスト3件が合格。
- Fresnel／方向／重み／期待エネルギーの28,672サンプルをWARPと実GPUで検証。
  通常入射・斜入射・接線方向、IOR=1から100の28条件。D3D12 ERRORなし。
- 実描画9キャプチャで、限定した不透明新旧比較、白の透過、着色、裏面、2枚重ね、
  factor=0、NRD非実行とNative同値を確認。全キャプチャでD3D12 ERRORなし。
- `validate_thin_transmission.py` は生成したUnlit背景板の中央ROIで評価する。
  RGB最大絶対誤差の事前許容値は0.001、完全同値の比較は0。
  実測: 白0、着色0.000293、裏面0、2枚重ね0.0000391、factor=0は0、NRD切替0。
  これは限定した入力・経路の結合確認であり、任意のシーンの画質合格ではない。
- レポート: `bin/PathTracingValidation/thin-integration-20261024/report.json`。
  SHA-256: `c81adbb8f4f6adb6963852c8dcfa2c023daf02840719f0bcf448ee527dfecd6f`。

専用ビルド: `bin/PathTracingValidation/thin-transmission-build-20261024/Debug/RtPbrSurvey.exe`。
次はScene EditorでのRGBA・IOR編集、定数glTFの限定読込み、Git管理の3シーンを追加する。
色付きの影、屈折、厚み、ユーザー目視確認は依然として未完了。
