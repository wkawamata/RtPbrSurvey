# テストシーンエディタ仕様案

作成日: 2026-09-10。状態: 実装前の提案仕様。今回の成果物は仕様書であり、以下の新規UI・JSON・CLIは未実装。
対象ワークスペース: `C:\work\RtPbrSurvey`（今回確認した実体）。

## 1. 目的と推奨構成

glTF、単純形状、PBRマテリアルを配置し、位置・回転・スケールと親子関係をJSONへ保存する。保存した配置・カメラ・環境・描画設定で同じテスト条件を再現する、小規模なレンダリング検証用エディタとする。

**編集用の軽量シーングラフを追加し、描画時には既存のフラットなインスタンス配列へ変換する方式を推奨する。** GPUリソースや描画パスを編集データに持たせない。描画プリセットは別JSONとし、シーンから相対パスで参照する。

主な利用例は、DamagedHelmetと金属球・粗い床を並べた反射比較、親グループを回転させた遮蔽比較、同一シーンを異なる描画プリセットでキャプチャする検証。物理、ゲームロジック、汎用ECS、アニメーション編集は初版に含めない。

## 2. 参照した内容と現状

TankPhysicsSandboxのタスク「マップエディタを追加」（ID: `01a078e5-e9d1-74a3-9d61-dd7250e1cd4f`）と、以下のローカル文書を参照した。

- [Map Editor仕様](../../TankPhysicsSandbox/Docs/feature/map-editor-spec-2026-09-07_jp.md)
- [Map Editor進捗](../../TankPhysicsSandbox/Docs/feature/map-editor-progress-2026-09-07_jp.md)

採用する考え方は、フォルダ単位の文書、相対アセット参照、数値編集から始めるUI、明示保存、失敗時の既存状態保持、段階実装。Tank固有のVisual/Hit命名規約、Jolt、開始地点、ゴール判定は導入しない。一般的なglTFをそのまま配置できることを優先する。編集元とビルド出力内コピーが食い違った事例を踏まえ、開いているシーンの完全パスを表示し、プレビューにも同じ文書を渡す。

| 現在の実装 | 確認内容と設計への影響 |
| --- | --- |
| `Scene/Scene.h` | `Scene.instances` はworld / prevWorld / materialId / meshIdを持つ配列。永続ID・名前・親子関係はない |
| `Scene/SceneBuilder.h` | Cube / Sphere / Cylinder、glTF追加、Material追加、Mesh別Instance追加がある。Plane生成は追加が必要 |
| `GltfLoader.h` / `doc/branch/feature/host-gltf-node-meshes.md` | 全体glTFロードはdefault sceneを平坦化。ノード抽出は名前指定で、重複名を拒否。Tank側の拡張APIがこの作業ツリーにもあるとは仮定しない |
| `Runtime/SceneRendererSettings.h/.cpp` | schemaVersion付きの描画設定JSON変換がある。Shadow、Hybrid Reflection、ToneMap、DLSS SR/RR等を含む |
| `Runtime/SceneRenderer.h` | CaptureSettings / ApplySettings / ReloadSceneResources等がある。再利用候補 |
| `App/SceneConfig.h/.cpp` | カメラ・環境・描画設定・既存サンプル固有値をまとめて保存。キーはシーン名、Gridは別接頭辞 |
| `Assets/Config/scene_config_default.json` | 既存サンプルごとの既定値。純粋な描画プリセットだけのファイルではない |

ユーザー設定の基本保存先は `%APPDATA%/RtPbrSurvey/scene_config.json`。既存Mergeはユーザーエントリ全体を採用する実装であり、フィールド単位の差分マージではない。この挙動を新しいシーンへそのまま適用すると、保存した検証条件がローカル設定で変わるため分離する。

## 3. 初版の機能範囲

| 初版で実装する | 後続で検討する |
| --- | --- |
| TopMenu → SceneEditor、New Creation / Edit / Load / Save / Save As、シーン一覧から実行 | Prefab、外部Materialライブラリ、アセット自動インポート |
| Empty / glTF / Cube / Sphere / Plane / Cylinder | ライトの複数ノード化、スキニング、アニメーション |
| Hierarchy選択、追加、複製、サブツリー削除、親変更 | 3Dピッキング、移動回転ギズモ、複数選択 |
| ローカル位置・回転・スケールの数値編集 | 任意のshear行列、負スケール、glTF内部階層編集 |
| Primitive用の共有PBR Material作成・割当 | glTFのMaterialスロット単位のoverride、独自Shader |
| Camera / Environment保存、描画プリセット参照 | キーフレーム、Capture PlanのGUI編集 |
| Undo / Redo、未保存保護、即時プレビュー | 大規模シーン向け非同期ストリーミング |

Materialは配置ノードではなく参照リソースとする。Materialを空間で比較したい場合は、そのMaterialを割り当てたSphere等を配置する。

## 4. ファイル構成と参照

```text
Assets/
  Scenes/ReflectionLab/scene.json
  Scenes/ReflectionLab/Models/          # シーン専用モデルを置く場合
  RenderPresets/deferred-reference.json
  Models/DamagedHelmet/DamagedHelmet.gltf
```

- シーン単位は `scene.json` を含むフォルダ。New Creationはメモリ上で作成し、最初のSaveでフォルダ・JSONを作る。Loadはフォルダを指定し、scene.json不在ならエラーとする。
- scene.json内のパスは当該JSONの親フォルダ基準。例の `../../Models/...` はAssets/Modelsへ解決する。`/` 区切り・UTF-8を保存形式とし、絶対パスは保存しない。
- アセットはシーンフォルダまたはアプリが明示登録したアセットルート配下へ解決できるものに限定する。既定ルートはAssets。`..` 自体を禁止せず、正規化後の到達先を検査する。glTF外部BIN・画像にも同じルート方針を適用する。
- Save Asは参照先を維持するよう相対パスを再計算する。依存アセットのコピーはしない。許可ルート外になる場合は保存前に診断する。持ち運び用Package Exportは後続。
- シーン一覧はAssets/Scenes直下のシーンフォルダを走査する。Loadした外部フォルダは当該セッションにも登録し、実行時は編集元を使う。再起動後の最近使った一覧はローカルUI設定に保存する。

## 5. Scene JSON v1案

次の例は新規フォーマットの提案であり、現在のアプリが読める形式ではない。

```json
{
  "schemaVersion": 1,
  "sceneId": "reflection-lab-001",
  "name": "Reflection Lab",
  "renderPreset": "../../RenderPresets/deferred-reference.json",
  "assets": [
    { "id": "helmet", "type": "gltf", "path": "../../Models/DamagedHelmet/DamagedHelmet.gltf" }
  ],
  "materials": [
    { "id": "metal", "name": "Polished Metal", "baseColor": [0.8, 0.8, 0.8, 1.0], "metallic": 1.0, "roughness": 0.15 },
    { "id": "floor", "name": "Rough Floor", "baseColor": [0.3, 0.3, 0.3, 1.0], "metallic": 0.0, "roughness": 0.8 }
  ],
  "nodes": [
    { "id": "group", "name": "Test Group", "parentId": null, "type": "empty", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1], "visible": true },
    { "id": "helmet-01", "name": "Helmet", "parentId": "group", "type": "gltf", "assetId": "helmet", "translation": [-1.5, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1], "visible": true },
    { "id": "sphere-01", "name": "Metal Sphere", "parentId": "group", "type": "primitive", "primitive": { "kind": "sphere", "radius": 0.5, "stacks": 24, "slices": 32 }, "materialId": "metal", "translation": [1.5, 0.5, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1], "visible": true },
    { "id": "floor-01", "name": "Floor", "parentId": null, "type": "primitive", "primitive": { "kind": "plane", "width": 10, "depth": 10 }, "materialId": "floor", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1], "visible": true }
  ],
  "camera": { "position": [0, 2, -6], "target": [0, 0.8, 0], "up": [0, 1, 0], "projection": "perspective", "verticalFovDegrees": 60, "nearZ": 0.1, "farZ": 1000 },
  "environment": { "source": "procedural", "iblEnabled": true }
}
```

### ID・階層

sceneIdは生成時にUUID相当の一意文字列を発行する（例は説明用の固定ID）。名称変更・通常保存では保持し、Save Asで別シーンを作る際は新規発行する。node / asset / materialのIDは各配列内で一意、不変とし、配列添字や表示名を参照キーにしない。

nodesはフラット配列でparentIdだけを保存し、childrenは保存しない。複数ルート可、1ノード1親、循環禁止。配列順をHierarchyの兄弟表示順とし、親が配列上で後ろでもロードできる。描画ノードも子を持てる。

複製はサブツリーへ新IDを発行し、内部parent参照を置換する。アセット・Materialは共有を維持する。削除はサブツリー単位のUndo可能な操作とする。共有Materialの削除は参照がある間は拒否し、Inspectorから「複製して割当」を提供する。

### Transform

左手系、+X右・+Y上・+Z前、距離はメートル。translation / rotation / scaleは親に対するローカル値。rotationの保存は正規化Quaternion `[x,y,z,w]`。UIは度数法のPitch(X) / Yaw(Y) / Roll(Z)で入力し、DirectXMathのroll→pitch→yaw規約でQuaternionへ変換する。Euler値への再変換は表示用で、毎フレーム保存値へ書き戻さない。

行ベクトル規約で `Local = S * R * T`、`World = Local * ParentWorld`。ルートの親行列は単位行列。glTF内部変換と左右座標変換はローダー側で一度だけ適用し、編集用Worldをその後に掛ける。SceneBuilderへ渡す行列を事前転置しない（AddInstanceが転置する）。

scaleは各軸正の有限値。非一様scaleは葉ノードのみ許可し、子を持つノードは正の一様scaleに制限する。これにより親子合成のshearを避ける。子追加・親変更時にも検査する。葉の非一様scaleは法線の逆転置とRT変換の整合性検証を出荷条件とする。

親変更は「World維持」を既定とし、`NewLocal = OldWorld * inverse(NewParentWorld)` をTRS分解する。再合成誤差が許容値を超える場合や循環は拒否し、変更前の状態を維持する。「Local維持」も明示操作として用意する。非表示は親から子へ継承し、非表示サブツリーを描画・TLASから除く。

### Primitive・Material・glTF

Primitiveは原点中心。Cubeはsizeが一辺、Sphereはradius、PlaneはXZ面・法線+Y・width/depthが全長、CylinderはY軸・radius/height・上下capあり。Sphereのstacksは2以上、slicesとCylinderのradialSegmentsは3以上。生成量の上限も実装時に設ける。

初版Materialは不透明のbaseColor（線形RGB、alpha=1）、metallic、roughnessを必須とし、それぞれ[0,1]。テクスチャ指定とemissiveの作者向け編集は後続。既存SceneMaterialにbaseColorFactorはないため、単色テクスチャ生成APIへ色空間を明示して変換するアダプターが必要。既存MaterialをそのままJSON dumpする設計にはしない。

glTFはdefault scene全体を1つの配置単位とする。内部の階層・元Materialを保持した見た目でロードし、内部ノードをこのJSONへ展開しない。既存AddGltfMesh経路を再利用する。glTFの頂点materialIdが存在するため、InstanceのmaterialId変更だけで全Materialを置換できるとは扱わない。初版ではglTFノードへのmaterialId指定を拒否する。対応範囲外のglTF機能はロード時に診断し、静的Meshの対応fixtureで保証範囲を固定する。

Cameraは1台でノード階層外。位置・target・upはWorld値。orthographicの場合はverticalFovDegreesの代わりに正のorthographicHeightを必須とする。positionとtargetの一致、視線と平行なup、不正clipを拒否する。編集カメラの移動はローカルUI状態とし、「Use Current View as Scene Camera」で文書へ反映する。

Environmentは初版proceduralのみ。既存SceneEnvironmentConfigのパラメータ群（skyColor、groundColor、lightColor、lightDirection、各intensity、lightSize、horizonSharpness等）を同名で任意指定でき、省略値はv1既定値表として固定する。iblEnabled既定はtrue。通常ロードでは環境を生成し、編集中の更新方針はUI状態とする。環境光源とrenderer.lightingの直接光源は別項目として表示し、自動同期しない。

## 6. 描画プリセットとの関連付け

renderPresetは必須参照とする。中身は `SceneRendererSettingsToJson()` のフォーマットを再利用し、独自の二重スキーマを作らない。初版の新規プリセットはCaptureSettingsの全保存対象値を出力する。パス・ToneMap・Shadow・Hybrid Reflection・SR/RR等の意味は既存Runtime型に合わせる。既存シリアライザーにない将来の設定が自動保存されるとは保証しない。

適用順は次の通り。

1. シーン・アセット・プリセットを候補データへ読み込み、型・参照・機能条件を検証。
2. シーンのカメラと環境を設定し、環境リソースを用意。
3. コード既定値を基にプリセットをデシリアライズして描画設定を適用。
4. 明示CLI overrideを適用。互いに競合する引数は診断する。
5. GPU能力判定で実効設定を決定し、保存要求値と実効値をUI・ログに別表示。

新しいファイルシーンへ既存scene_config.jsonのシーン名エントリを自動適用しない。個人のウィンドウ配置・最近使ったフォルダ等はsceneIdで別管理し、見た目を変える設定は含めない。初版ではscene側renderer overrideも設けず、比較用にはプリセットを複製する。

Rendererパネルでの変更は作業中のプリセット値となり、シーン文書とは別のdirty状態を持つ。「Save Preset」は表示中の参照先へ明示保存し、共有プリセットが他のシーンにも影響する旨を表示する。「Save Preset As」は新ファイルを保存後にrenderPreset参照を変更する。通常の「Save Scene」はプリセットを上書きしない。プレビュー中の一時変更と未保存Presetの状態も画面に示す。

新規シーンには全値入り既定プリセットのコピーをシーンフォルダへ作る案を採用し、共有利用は後から参照選択する。既存scene_config_default.jsonからの移行は、選んだエントリのrendererをプリセットへ、camera/environmentをSceneDocumentへ抽出する明示操作とする。meshScale等のサンプル固有値を自動的にnodeへ変換しない。既存サンプルの保存処理は継続する。

SR/RR未対応環境では対話プレビューのみ既存fallbackを使い、その理由を表示する。厳密キャプチャでは要求した機能が使えなければ失敗終了とする。native RRの実験的実行許可は既存ガードを維持し、通常プリセットのロードだけで無条件に有効化しない。

## 7. UIと保存の操作仕様

### 7.1 TopMenuからの画面遷移

```text
TopMenu
  └─ SceneEditor
       ├─ New Creation → 新規シーンを作成 → Edit
       ├─ Load → 既存シーンを読込 → Edit
       └─ Back to TopMenu → TopMenuへ戻る

Edit
  ├─ glTF / Primitive / Material / Transform / 親子関係を編集
  ├─ Camera / Environment / 描画Preset参照を編集
  ├─ Save → 保存してEditを継続
  ├─ Load / New Creation → 未保存保護後に文書を切替
  └─ Back to TopMenu → 未保存保護後にTopMenuへ戻る
```

TopMenuに `SceneEditor` 項目を追加する。初回遷移時は文書未選択のエディタ開始画面を表示し、New Creation / Loadを選べるようにする。空のシーンを暗黙生成・保存しない。文書未選択ではSaveと編集操作を無効にする。

| 操作 | 動作 |
| --- | --- |
| New Creation | シーン名を入力し、新しいsceneId、空のnodes/assets/materials、既定Camera/Environmentと作業用Presetで文書を作成。Editへ進む。ファイルは最初のSaveまで作成しない |
| Edit | 作成またはLoadした文書をHierarchy / Inspector / Previewで編集する状態。別ファイルの選択や実行モードへの遷移を意味しない |
| Load | シーンフォルダを選び、scene.json・参照アセット・Presetを検証して読み込む。成功したらEditへ進む。キャンセル・失敗時は現在の文書と選択を維持する |
| Save | 初回は保存先を選び、新規シーン専用Presetを先に保存してからscene.jsonを保存する。保存済みシーンは同じscene.jsonへ保存し、Editを継続する。既存Presetの変更はSave Presetで保存する |
| Save As | 別シーンとして新しいsceneIdで保存する。既存文書の保存先を無言で上書きしない。参照パスの扱いは第4節に従う |
| Back to TopMenu | 未保存保護を通してTopMenuへ戻る。保存済みシーンの一覧を更新し、編集したシーンの保存先を選択状態にする |

Save完了時は保存先と成功状態を表示する。保存先選択のキャンセルや書き込み失敗ではdirtyを解除しない。初回Preset保存後にScene保存が失敗した場合は、Presetの保存結果を示してEditを継続し、再試行できるようにする。

### 7.2 編集画面と保存保護

```text
SceneEditor    New Creation | Load | Save | Save As | Undo | Redo | Back to TopMenu
Document: <完全パス>    Scene: Modified    Preset: Saved
Hierarchy              3D Preview                  Inspector
  Floor                 grid / axes                Name / Visible
  Test Group                                       Parent
    Helmet                                         Local T / R / S
    Metal Sphere                                   Asset / Primitive / Material
Assets / Materials                                 Camera / Environment
Renderer: <preset path>   Save Preset | Save Preset As | Reload Preset
```

初版は既存ImGuiと描画画面を使い、数値入力で編集する。選択対象のbounds・座標軸・グリッドはエディタ表示専用とし、保存シーンや自動キャプチャに含めない。Hierarchy以外の3D選択やギズモ用ライブラリ導入は後続。

Undo / Redoは追加・削除・複製・親変更・Transform・Material・カメラ反映・Preset編集を対象とする。数値ドラッグは開始から終了まで1操作。保存地点を履歴に記録してdirtyを判定する。上限100操作を初期値とし、GPU資源は履歴に保持しない。

Load / New Creation / Back to TopMenu / 終了では、未保存のSceneとPresetを列挙してSave and Continue / Discard / Cancelを提供する。Save and ContinueはPreset保存成功後にScene保存を行い、失敗時は遷移せず未保存対象を保持する。複数ファイルの完全な原子保存は初版では保証しないため、保存済み対象も明示する。

各JSONは同一ディレクトリの一時ファイルへ書き、flush/close後に置換する。失敗したら元ファイルを残す。外部変更を検出した場合は無言で上書きせず再読込か別名保存を選べるようにする。SceneとPresetのReloadは候補検証後に反映し、壊れたファイルで作業中文書を失わない。

## 8. 実装の責務と更新契約

以下のファイル名は追加候補。

| 層 | 責務 |
| --- | --- |
| `Scene/SceneDocument.h` | GPU非依存のID、Node、Asset、Material、Camera、Environment |
| `Scene/SceneDocumentJson.cpp` | JSON読書き、バージョン・値・参照検証、診断 |
| `Scene/SceneGraph.cpp` | 親子評価、World計算、複製、削除、親変更 |
| `Scene/SceneDocumentBuilder.cpp` | Asset cache、SceneBuilderへの変換、NodeId→Instance索引対応 |
| `App/SceneEditorSession.*` | 文書所有、dirty、Undo、ファイル操作、モード遷移 |
| `Ui/SceneEditorUi.*` | Hierarchy / Inspector等の表示と編集要求 |
| `App/RenderPresetStore.*` | プリセットファイルI/OとRuntime設定変換の接続 |

CPUの編集文書を唯一の正とする。Rendererから毎フレーム逆に文書を再生成しない。Builderが所有するSceneMeshはScene参照が使われる間生存させる。

Transform変更では子孫WorldとInstanceのみ更新し、geometryを毎フレーム再ロードしない。前フレームに実際に描画したWorldをprevWorldに保持する。新規追加・ロード直後はprevWorld=world。編集による不連続変更、Material・構造・Preset・Camera切替時は対応するtemporal historyをリセットする。この接続APIが不足する場合は明示的に追加する。

Mesh追加・削除・可視性変更等の構造変更は候補SceneBuilderを完成させてからフレーム境界でGPU資源を再構築する。操作確定時にまとめ、スライダー更新ごとに再構築しない。BLAS/TLASと表示数も同期する。GPU参照中の旧資源は完了待ちまたは遅延破棄する。GPU生成失敗時まで旧シーンを保持するには二段階commitが必要であり、既存ReloadSceneResourcesが原子的だとは仮定しない。初版の必須検証点とする。

同一アセット・同一PrimitiveパラメータはMeshを共有し、Instanceのみ増やす。永続IDからGPU indexへの対応は毎構築時に作り直せる一時データとする。

## 9. 検証とエラー仕様

JSON Schema v1を実装段階で追加し、構造検査とC++意味検査を併用する。未知schemaVersion・未知type・未知フィールドは拒否する方針とし、将来版を読み捨てて保存しない。ID重複、参照欠落、循環、自己親、非有限値、Quaternion長ゼロ、非正scale、不正Primitive分割数、範囲外Material、Camera不正値を検査する。Quaternionは長さが許容誤差内なら正規化し、大きな逸脱は拒否する。

エラーにはファイルパス、JSON位置（例: nodes[2].parentId）、該当ID、原因を含める。アセット・Preset欠落は初版ではシーンLoad全体を失敗とし、壊れたモデルを無言で省略しない。

| 段階 | 合格条件 |
| --- | --- |
| データ | Save→Loadで意味が同一。配列順によらない親評価、循環・欠落・不正値の拒否、失敗時出力保持 |
| Transform | 3階層の移動・90度回転・一様scaleを独立した期待座標で検証。World維持の親変更、拒否操作の非破壊性 |
| Builder | 同一glTFの2配置、元Material保持、Primitive色/roughness/metallic、planeの面向き、Node対応 |
| 保存 | 保存失敗・外部変更・日本語パス・Save As参照再計算・共有Presetと個別dirty |
| UI | TopMenu→SceneEditor→New Creation→Edit→Save→TopMenu→SceneEditor→Loadの往復。追加→親変更→Material編集→Undo/Redo→保存→再Load→同じ表示。未保存終了の3選択 |
| 描画 | Debug x64ビルドとD3D12 Debug Layer。追加・削除・非表示・親移動後のRT shadow/reflection一致 |
| 再現性 | 同一Scene/Preset/CameraでCLI capture。ローカル設定に左右されず、能力不足を検出 |

初版の性能目標は100配置程度を数値編集できること。厳密なFPS目標は実装測定後に設定する。画像比較は環境・GPU・解像度・warm-up・seedを固定し、temporal処理に対して無条件のビット一致を要求しない。

## 10. 自動テスト用CLI案

次の3引数は新規提案。既存CLIにはまだ追加されていない。

- `-SceneFile <path>`: ファイルシーンを読み、Scene Cameraで実行する。
- `-RenderPreset <path>`: 当該実行だけ参照Presetを差し替える。CLIパスは起動ディレクトリ基準で絶対化し、JSONへ書き戻さない。
- `-StrictSceneSettings`: 要求した描画機能が利用できない場合は非ゼロ終了。

既存の `-CapturePath` / `-CaptureAfterFrames` / `-ExitAfterCapture` / `-LogToFile` と組み合わせる。-SceneFileと既存自動シーン選択フラグの同時指定はエラーにする。ReflectionCapturePlanはシーンロード後に既存規約を適用する。CLI overrideの最終値・シーン/プリセットの絶対パスと内容ハッシュ・GPU・解像度を実行ログへ記録し、比較条件を追跡可能にする。

## 11. 開発プラン

開発は、保存形式を確定してから、非UIのロード・描画・キャプチャ経路を通し、最後にエディタ操作を接続する。各Stepは単独でビルド可能、かつ前Stepのテストを維持する差分とする。

```text
基盤（Step 1-3）
  → 実行可能なScene（Step 4-6）
    → TopMenuと編集操作（Step 7-10）
      → 仕上げ（Step 11以降）
```

| フェーズ | 到達点 |
| --- | --- |
| 基盤 | JSONのSceneDocumentを検証して読み書きでき、親子World行列を評価できる |
| 実行可能なScene | SceneDocumentから既存レンダラーのSceneへ変換し、Presetを適用してCLIキャプチャできる |
| 編集操作 | TopMenuからNew Creation / Edit / Load / Saveができ、編集結果を再読込できる |
| 仕上げ | Undo/Redo、Save As、外部変更・失敗の診断、D3D12・画像回帰を完了する |

### 最初の10 Step

| Step | 範囲 | 主な成果物 | 完了条件 |
| --- | --- | --- | --- |
| 1 | **SceneDocumentの型を定義する**。Node、Asset、Material、Camera、Environment、ID、PrimitiveをGPU非依存で表す | `Scene/SceneDocument.h` と固定のC++テストデータ | 空の文書とHelmet・Sphere・Planeを持つ文書を生成でき、IDの一意性と初期値を単体テストで確認できる |
| 2 | **JSON v1を読み書きする**。schemaVersion、相対パス、必須値、診断情報を実装する | `Scene/SceneDocumentJson.*`、fixture `Tests/Fixtures/Scenes/reflection-lab/scene.json` | fixtureのLoad→Save→Loadで意味が一致し、欠落ID・不正値・未知versionで元の出力を壊さずエラーを返す |
| 3 | **シーングラフを評価する**。parentIdからWorld行列を求め、循環・自己親・参照切れを拒否する | `Scene/SceneGraph.*` と親子Transformテスト | 3階層の移動・90度回転・scale、World維持の親変更、拒否時の非破壊性を期待値で確認できる |
| 4 | **PrimitiveとMaterialを既存SceneBuilderへ変換する**。Empty、Cube、Sphere、Plane、Cylinderと単色PBR Materialを扱う | `Scene/SceneDocumentBuilder.*` のPrimitive経路 | NodeIdとInstanceの対応を作れ、Planeの向き、Material値、複数InstanceのMesh共有をCPUテストで確認できる |
| 5 | **glTF Assetを変換する**。default sceneを1配置単位で読み、既存Materialを保ったままSceneへ組み込む | `SceneDocumentBuilder` のglTF経路とHelmet fixture | 同一glTFを2回異なるTransformで配置でき、参照失敗では候補Sceneを公開せず診断を返す。DebugビルドとD3D12 Debug Layerを確認する |
| 6 | **ファイルSceneを実行する**。`-SceneFile`で文書をLoadし、Camera/Environmentを反映して既存サンプル選択を迂回する | AppのCLI解析・Sceneロード経路、Smoke用Scene fixture | Helmet・Sphere・PlaneのSceneを起動し、`-CapturePath`と`-ExitAfterCapture`でPNGを生成できる。既存自動シーン選択フラグとの競合は明確に失敗する |
| 7 | **RenderPresetを接続する**。`SceneRendererSettings`を別JSONから読み、Scene固有のCamera/Environmentと分離して適用する | `App/RenderPresetStore.*`、既定Preset fixture、`-RenderPreset` | 同一Sceneで2つのPresetを切替可能。既存`scene_config.json`がファイルSceneの見た目を上書きしない。能力不足時の実効値と理由をログへ出せる |
| 8 | **TopMenuとSceneEditorの殻を追加する**。TopMenuからSceneEditor開始画面へ入り、New Creation / Load / Backを実装する | Appのモード遷移、`Ui/SceneEditorUi.*` の開始画面 | 文書未選択では編集・Save不可。New Creationはメモリ文書を作ってEditへ、Loadは検証成功後だけEditへ進み、BackでTopMenuへ戻れる |
| 9 | **Edit画面を最小構成で接続する**。Hierarchy、Inspector、Previewを表示し、Primitive追加・選択・Transform数値編集・削除を反映する | `App/SceneEditorSession.*`、Hierarchy/Inspector UI | 追加→選択→位置/回転/scale変更→Preview更新→削除ができる。構造変更はフレーム境界でGPU資源を安全に更新し、temporal historyをリセットする |
| 10 | **Load / Saveを完成させる**。初回Save、既存Save、Save As、未保存保護、編集元パスの表示を実装する | ファイルダイアログ接続、dirty管理、原子的書込み | TopMenu→New Creation→Edit→Save→TopMenu→SceneEditor→Loadで同じ表示を再現できる。保存失敗・Load失敗・キャンセルは編集中文書を保持し、未保存遷移でSave / Discard / Cancelが機能する |

Step 6で最初の縦断到達点、すなわち「JSONのHelmet・金属球・床を読み込み、CLIでキャプチャする」を達成する。Step 7で描画条件の再現性を加え、Step 10で利用者がTopMenuからSceneを作成・編集・読み書きできる状態にする。

### Step 11以降の予定

1. Undo / Redo、数値ドラッグの操作単位、親変更（World維持 / Local維持）、Material複製と共有参照の編集を追加する。
2. シーン一覧、最近使った外部Scene、アセット選択、Save Preset / Save Preset Asを追加する。
3. 外部変更検出、より詳細な診断、Editor用Grid・Axes・選択Bounds、3Dピッキングとギズモを追加する。
4. `-StrictSceneSettings`、設定ハッシュのログ、D3D12 Debug Layer、固定条件の画像比較をCIまたは手動品質ゲートへ接続する。

## 12. 提案判断と残る技術確認

仕様案として、軽量Graph、Quaternion保存、葉のみ非一様scale、glTF内部編集なし、MaterialはPrimitive中心、Preset必須参照、数値UI先行を採用した。これにより初版の範囲を小さく保ちながら、親子配置と再現性を満たす。

実装前半で確認すべき点は、既存更新経路でのprevWorld/temporal reset、GPU資源置換失敗時の保持、非一様scaleの法線とTLAS、色空間変換、glTF対応機能のfixture範囲、Environment v1省略値表である。推測で完了扱いにせず、それぞれ段階2〜3の完了条件へ含める。

今回の調査ではソースと関連仕様を確認した。アプリ実装・ビルド・GPU実行検証は行っていない。
