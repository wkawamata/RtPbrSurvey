# 別PC向け作業案: 複数光源・複数光源タイプ

## 目的と推奨する依頼範囲

平行光源（Directional）、点光源（Point）、スポット光源（Spot）を同一シーンで混在させ、UIから追加・編集・削除し、保存・再読込できるようにする。

別PCへの最初の依頼は下記の「段階1」とする。複数光源すべての影、面光源、多数光源の高速化まで一度に含めず、レビュー可能な単位に分割する。本書は作業依頼の設計資料であり、実装・別PCへの送信はまだ行っていない。

## 調査した基準と現状

- 調査元: `C:\work\RtPbrSurvey-work-3`、HEAD `608be5a9dd50558f37a32db60de99e5eeb75bfe2`。
- 別PCでは実際のcheckoutパスと開始コミットを記録する。AGENTS.mdの旧workspace表記をそのまま編集先と解釈しない。
- `Engine/RtPbrSurveyEngine.h` の `LightingParams` / `LightingConstants` は単一方向・単一色を保持する。
- `Shaders/shaders_LightPass.hlsl` は単一光源の直接光にShadow Maskを掛ける構成。単にループ化して同じマスクを全光源へ掛けてはいけない。
- `Shaders/shaders.hlsl` は `normalize(-lightDirection)`、Deferred側は `normalize(lightDirection)` を使用する。方向の意味の不一致を先に確認する。
- 直接光の利用箇所はForward、Deferred、Reflection Evaluate、Reflection Ray Hit Debug、Path Tracing、Ray Query Shadowなどに分散している。
- UIは主に `App/DebugUi.cpp`、保存は `Runtime/SceneRendererSettings.*`、`App/SceneConfig.cpp`、`App/RenderPresetStore.*` を確認する。
- Scene Documentはv1で未知フィールドを拒否する。EnvironmentのlightDirection/lightColorは環境マップ生成用で、直接光とは別物。

## 段階1: 光源配列と3種類の直接光（今回の推奨範囲）

### データと責務

CPU側にLightTypeとLightの定義、GPU側に対応するLightGpuDataを設ける。既存の責務分割に合わせて配置し、UI専用データをGPU構造体へ混ぜない。

| 項目 | 意味 |
| --- | --- |
| id / name | 編集・選択用の安定IDと表示名。配列indexを永続IDにしない |
| type / enabled | Directional / Point / Spot、有効・無効 |
| color / intensity | 線形RGB、非負の光源別強度 |
| position | Point / Spotのワールド位置 |
| direction | 光が進むワールド方向。DirectionalではsurfaceToLight = -direction |
| range | Point / Spotの有限影響半径。正の値 |
| innerCone / outerCone | Spotの内外半角。UIは度、GPUはcos値 |

初版の上限は合計16灯を推奨する。0灯、無効灯を含む扱いを決め、超過入力を黙って切り捨てずエラー表示する。全体のDirect Lightスイッチ、IBL、Emissive設定は光源配列と分離する。旧diffuseIntensityは既存では直接光のradianceに使われるため、名前だけから拡散項専用と解釈しない。

方向ベクトルは正規化し、ゼロ長・非有限値を拒否する。色・強度の負値、range <= 0、不正な円錐角を検証する。円錐角は `0 <= inner < outer < 90度` とし、同値による除算を避ける。

### 直接光の評価

- Directional: 位置に依存せず、距離減衰なし。
- Point: surfaceToLightは光源位置との差。距離の二乗に反比例する減衰とrange境界で0になる滑らかな減衰を組み合わせる。
- Spot: Pointの減衰に円錐係数を掛ける。光源から受光点への方向とdirectionで角度を評価し、外側は0、内側は1、その間を滑らかに補間する。
- ゼロ距離付近の下限を定数として明示し、NaNや無限大を出さない。
- 初版の強度はプロジェクト内の相対値とし、Directionalは一定radiance、Point / Spotは距離1での基準値と定義する。物理単位対応を装わない。
- 方向・減衰・radianceの計算を共通HLSLへ集約する。既存PBR BRDFは維持し、IBLとEmissiveは光源数によらず1回だけ加算する。

### GPUへの転送

16灯の固定長CBVを第一候補とする。StructuredBufferが適切なら理由を報告して採用してよい。CPU/HLSLのサイズ・offset・paddingを検証し、光源数は整数で渡す。フレームごとのリソース更新を既存の同期方式に合わせ、描画中の領域を書き換えない。LightingConstantsを複製している各shader、root signature、bufferサイズを一括で点検する。

### 各描画経路の扱い

| 経路 | 段階1の必須動作 |
| --- | --- |
| Deferred | 3種類を同時評価し直接光を合算 |
| Forward | 同じ光源配列と方向・減衰を使用。既存の簡易BRDFのPBR化は別件 |
| Reflection Evaluate / Ray Hit Debug | ヒット位置で同じ光源配列の直接光を評価。全光源の影対応は段階2 |
| Path Tracing | 全灯を列挙して直接光を評価し、既存の可視性評価を光源別に適用。Point / Spotは光源距離をrayの上限にする |
| Ray Query Shadow / Shadow Debug | 指定した主Directional 1灯の既存マスクを維持 |

Path Tracing初版は少数灯の全列挙とし、確率的な光源選択やMIS拡張は行わない。灯数で平均して暗くしたり、IBLを灯数分加算したりしない。既存経路間のBRDFや遮蔽の違いは残り得るので、画像の完全一致は要求しない。

### 影の段階的な扱い

既存Shadow Maskは安定IDで指定した主Directionalの寄与にのみ適用する。主光源の削除・無効化・型変更時はマスクを無効にし、別光源へ暗黙に付け替えない。追加灯の影は段階1のDeferred / Forwardでは未対応とUIに表示する。Point / Spotだけの構成でも、旧Directionalの影が残ってはいけない。

### UI・保存・履歴

- 一覧、選択、追加、複製、削除、有効切替、名前・型・色・強度・位置・方向・range・円錐角の編集を提供する。型に必要な項目だけ表示する。
- 光源設定の変更・追加・削除でPath Tracing accumulationと関連するReflection temporal historyを無効化する。不要な全リソース再生成は避ける。
- 段階1は既存renderer.lightingの保存経路を拡張し、Scene Documentに新しいlights構造は追加しない。プリセットに置く暫定設計であることを記録する。
- 旧単一光源設定はDirectional 1灯へ変換する。color、直接光強度、有効状態を保持し、directionは既存Deferredの見た目を基準に符号を変換する。Forwardの既存符号不一致は修正前後を報告する。
- 新形式の光源配列が空なら0灯を維持し、旧光源を自動復活させない。新旧フィールドが混在する入力の扱いを明示し、曖昧な混在は拒否する。
- 実際のRenderer設定のversion管理に従って形式を更新する。未知形式・不正値のLoad失敗時は現在の有効設定を保持する。
- Environmentの擬似太陽と直接光を自動同期しない。

## 段階2以降（今回の完了条件には含めない）

1. 複数光源の影: 光源ごとのvisibilityを保持し、Point / Spotの有限長shadow ray、反射ヒット位置での遮蔽、コスト計測を追加する。既存単一マスクの流用では実現できない。
2. シーン所有への移行: Scene Documentのlightsと必要ならNode参照を設計し、schemaVersionを更新する。`doc/scene-document-schema-policy_j.md` に従ってv1移行と設定の優先順位を定義する。
3. 面光源: Rect / Diskのサイズ・向き、積分手法、ソフトシャドウを別課題として設計する。
4. 多数光源: 実測後にTiled / Clustered、光源選択samplingを検討する。
5. glTF punctual lightsのimport、IES、物理単位、光源gizmoは個別の追加課題とする。

## 検証と完了条件

大型外部アセットに依存せず、床・遮蔽物・roughnessの異なる球を使う再現用シーンまたはプリセットを追加する。

- [ ] 旧1灯のDeferred画像を同じcamera・exposureで比較し、互換を確認する。
- [ ] 赤と青のPointを左右に置き、両方の寄与と距離減衰を確認する。
- [ ] Spotの内側・境界・外側、向き変更、range外を確認する。
- [ ] Directional / Point / Spot混在、0灯、全無効、16灯、17灯の拒否を確認する。
- [ ] 主Directionalの影が追加灯へ掛からず、削除・型変更後に古い影が残らない。
- [ ] Forward、Deferred、Hybrid Reflection、Path Tracingで光源編集が反映され、履歴が適切にresetされる。
- [ ] 保存→LoadでID、型、値、無効状態、主光源指定を維持する。
- [ ] 旧形式変換、不正入力、未知type、上限超過、Load失敗時の状態保持をCPUテストで確認する。
- [ ] Debug x64 buildが成功し、3種類混在時のD3D12 Debug Layerに新規ERRORがない。WARNINGは内容を調べる。
- [ ] 同じPC・解像度・設定で1 / 4 / 16灯を計測し、FPSまたはframe timeを記録する。GPU計測をしていない場合、CPU FPSをGPU時間として報告しない。

スクリーンショットは既存のCapturePath / CaptureAfterFrames / ExitAfterCaptureを利用する。新しいテストシーンの自動選択が必要なら実装するか、実際に行った選択手順を記録する。存在しないCLIフラグを検証コマンドへ書かない。

## #02 PT実装との作業境界

この文書PRはmainを基点としたMD 1ファイルの追加のみとし、#02の `codex/path-tracing-nee-mis` のコミットや実装差分を含めない。#02のworkspace、branch、indexを操作しない。

#02はNEE / MIS、PDF、BSDF sampling、Path Tracingの積分・蓄積処理を担当する。別PCは光源データ、UI、保存、Raster / Hybridの直接光評価と、PT向け光源adapterを担当する。

#02側の `PathTracingLightSample` / `PathTracingDirectLightCandidate` 契約に接続する。参照時点では `4c30ae2` に定義されており、この文書PRのmainにはまだ含まれない。adapter実装は契約が統合された時点の定義に合わせる。契約を別PC側で重複定義したり独自変更したりしない。

- `Shaders/PathTracingSampling.hlsli`、`Shaders/shaders_PathTracing.hlsl`、`doc/branch/feature/path-tracing-design.md` の並行編集は避ける。PT本体への接続変更は#02側の統合タイミングに合わせ、独立した差分として扱う。
- adapterは光源方向、距離、入射radiance、delta属性などを契約へ変換する。方向の意味はsurface-to-lightに変換する。visibility / BRDF / cosineをradianceへ先に掛けない。
- 3種類はdelta lightとして扱い、連続方向PDFと離散選択確率を混同しない。全列挙時に光源選択確率の逆数を余分に掛けない。確率的選択とMISのweight計算は#02側に任せる。
- 上記の段階1に記したPT対応は最終的な統合完了条件である。接続待ちならRaster / Hybrid側を先行レビュー可能にし、PT統合待ちと明記する。未接続のまま全体完了とは報告しない。
- Engine本体、App/DebugUi.cpp、設定構造体も共有編集点なので、光源用の新規ファイルへ処理を分離し、既存ファイルの変更を接続箇所に限定する。

## 別PCとの分担・納品

別PCは光源データ、直接光評価、光源UI、設定保存、検証を担当する。他PCで作業中の機能は着手時に確認し、特にEngine本体・App/DebugUi.cpp・Reflection shaderの編集重複を共有する。光源用の新規ファイルへ処理をまとめ、関係のないrefactorや整形を混ぜない。

納品はレビュー可能な差分、検証手順、画像と計測の所在、既知制限、開始/終了コミットまたは作業状態の報告とする。報告には実際の編集workspaceとrepo外のtask/report/logフォルダを区別して記載する。UTF-8 BOMなし・CRLF・Allman・4 spacesを守り、include順序を変更しない。生成ログや一時画像はcommit対象にしない。commit / push / merge / reset / checkout / branch切替は別途依頼がある場合だけ実施する。

最終報告の末尾に `Status: done` または `Status: blocked` を付ける。未実施の検証、対応できなかった描画経路がある場合は明示し、段階1を完了扱いにしない。

## 2026-09-21: 本タスクでの実装方針・追加提案

実装workspaceは `C:\work\RtPbrSurvey`、開始コミットは `8786df8`。本タスクの使命は複数光源、複数光源タイプ、シーンエディタへの統合とする。開始時に存在した `Scene/SceneGraph.cpp` / `.h` の未コミット変更は別作業として保持する。

### 採用した方針

- 光源一覧・選択・追加・複製・削除・プロパティ編集を共通UIにし、Debug UI、renderer tools、Scene EditorのRender Presetパネルで再利用する。安定IDで選択と主Directionalを指定する。
- Scene Editorの光源は段階1ではrender presetに保存する。パネルに保存状態、Save Preset、保存先の説明を表示する。Scene Documentのノード所有・schema拡張は次段階とする。
- renderer settingsのschemaVersionを4にする。新形式は `lighting.lights` と `lighting.primaryShadowLightId`。v1～v3の旧単一光源フィールドはDirectionalへ変換する。新旧フィールド混在、未知version/type、不正な値、17灯以上を拒否する。
- GPUは最大16灯の固定長CBVとし、CPU/HLSLのoffset・サイズを検証する。方向・距離・減衰・円錐係数を共通HLSLに集約する。
- 追加光源の影はDeferred / Forwardでは未対応と表示する。主Directionalを削除・無効化・型変更した場合、UIは主光源指定を解除する。保存データが存在しないIDを指す場合も影マスクは無効になり、別光源へ自動付替えしない。
- `Assets/Scenes/MultiLightValidation/` に外部モデルを使わない検証シーンと4灯プリセットを追加する。赤・青のPoint、緑のSpot、Directional、roughnessの異なる球、床、遮蔽物で確認する。

### 検証中に判明した点

- 現在のScene Document builderはプリミティブの色テクスチャをEmissiveにも割り当てている。光源評価を見やすくするため、検証プリセットではIBLとEmissiveをOFFにする。この既存マテリアル生成挙動の修正は別の小さい差分として検討する。
- 1 / 4 / 16灯のCPUフレーム時間は表示同期の影響を受ける。GPUコストの比較には、同期条件を揃えたGPU timestamp計測を追加することを提案する。CPU FPSをGPU性能の根拠にしない。
- 現checkoutには `PathTracingLightSample` / `PathTracingDirectLightCandidate` 契約がまだない。現段階のPTは主Directionalだけの暫定接続であり、複数灯・Point / Spotは統合待ち。契約の重複定義は行わず、契約統合後に全灯のadapterと有限長visibilityを接続する。段階1全体の完了条件から外さない。

### 次段階の提案

1. 光源の位置・方向をviewportで確認できる表示とgizmoを追加する。Scene Documentの所有モデルを決めたうえで、ノード選択・Undo/Redo・保存を統合する。
2. Scene Editorの通常SaveとSave Presetの関係を整理し、シーンと光源を一緒に保存する操作を追加する場合は、部分失敗時の状態保持も設計する。
3. 主Directionalの影だけでなく、Point / Spotの光源別visibilityを追加し、Reflectionのヒット位置での遮蔽も扱う。対応前後の品質とGPU時間を比較する。

追加提案・採用結果は本節を更新する。ビルド・テスト・画像・計測の詳細と未完了項目は実装検証メモへ記録する。

### 2026-09-22: GPU計測提案の採用

既存GPU timestampを `-LogFPS` のログに併記する変更を実装した。1 / 4 / 16灯を各60サンプルで比較し、16灯/1灯の中央値比はLightPass約3.27倍、GPU計測区間total約1.15倍だった。Debug・単一シーン・単回実行の参考値であり、一般的な負荷倍率とはしない。詳細は `multi-light-validation_j.md` に記録。次の性能検証では選択adapter名・電力条件を明示し、実行順反転の反復計測を提案する。現時点では、この測定だけを根拠にタイル/クラスタ方式へ拡張せず、段階1の統合と未完了検証を優先する。
