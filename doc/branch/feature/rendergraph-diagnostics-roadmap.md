# RenderGraph診断機能ロードマップ

## 目的

既存のread-only RenderGraph node viewerを、巨大なgraphから問題箇所を素早く発見できるdiagnostics toolへ段階的に発展させる。

各taskは原則として、実装、Debug x64 build、必要なunit test、実画面確認、文書更新、commitまでを1単位とする。編集機能へ進むまではruntime RenderGraphを変更せず、snapshotである`RenderGraphDocument`とviewer側の状態だけを扱う。

## 実装順

### RG-01 選択ノード詳細パネル

- [x] Node selectionを取得する。
- [x] Pass詳細として実行順、Read/Write、stateを表示する。
- [x] Resource詳細として種別、lifetime、transient/persistent、使用Passを表示する。
- [x] Node内へ情報を追加しすぎず、独立したdetail paneに表示する。
- [x] 選択解除とgraph更新後の無効selectionを安全に処理する。

完了条件:

- PassとResourceを選択すると対応する詳細が表示される。
- Node位置、pin、link、runtime graphへ副作用がない。
- Debug x64 buildと実画面確認が成功する。

### RG-02 検索と表示フィルター

- [x] Pass名とResource名の大文字小文字を区別しない部分一致検索を追加する。
- [x] Pass、Texture、Buffer、Unknown Resourceの表示filterを追加する。
- [x] transient/persistent/unknown lifetime filterを追加する。
- [x] 選択ノードと直接接続されたnodeだけを強調するmodeを追加する。
- [x] Link endpointとlayoutを維持するため、非該当nodeは非表示にせず薄く表示する。

完了条件:

- 検索結果へfocusでき、filter中もlink endpointが破綻しない。
- Filter解除後にnode位置が維持される。

### RG-03 Resource lifetime timeline

- [x] Pass indexを横軸としたResource lifetime barを追加する。
- [x] first/last passとtransient/persistentを表示する。
- [x] 数値suffixを除いた論理名でsortingし、`.0`/`.1`など関連Resourceを隣接表示する。
- [x] 選択中Resourceとtimelineのselectionを同期する。

完了条件:

- Resourceの生成から最終使用までをpass順で確認できる。
- Ping-pong Resourceを別Resourceのまま比較できる。

### RG-04 State transition診断

- [x] Resourceごとの要求state列をpass順に構築する。
- [x] state transitionが必要なedgeを紫で強調する。
- [x] UAV barrier候補を黄色で識別する。
- [x] 現snapshotでは要求stateのみを診断し、redundant transitionと実barrierのstate不一致はruntime barrier event取得後に判定する方針とする。
- [x] 診断一覧にpass、resource、before/after stateを表示する。

完了条件:

- 少なくとも通常transitionとUAV関連を区別して確認できる。
- Warning判定に対するunit testがある。

### RG-05 GPU timing表示

- [ ] 既存GPU Work Meterからpass timing snapshotを取得する境界を設計する。
- [ ] Pass nodeとdetail paneへGPU時間を表示する。
- [ ] current、移動平均、最大値を切り替えられるようにする。
- [ ] frame全体に占める割合を表示する。
- [ ] timing未取得時を明確に表示する。

完了条件:

- Timing取得がrendering synchronizationへ影響しない。
- Pass名またはstable IDで安全に対応付けられる。

### RG-06 Ping-pong Resourceの論理グループ表示

- [ ] `.0`/`.1`を別nodeとして維持する。
- [ ] 論理名とphysical indexを表すgroup metadataを追加する。
- [ ] History ReadとCurrent Writeを強調する。
- [ ] Groupの折り畳み表示を検討する。

完了条件:

- Physical ownershipを失わず、現在のroleが一目で分かる。
- Role交換時もnode位置とBox寸法が安定する。

### RG-07 Snapshot保存と比較

- [ ] `RenderGraphDocument` snapshotのserialization形式を決める。
- [ ] 2 snapshot間のnode、link、state、lifetime差分を計算する。
- [ ] added、removed、changedをviewerで色分けする。
- [ ] Forward/Deferredや機能ON/OFF比較を想定したmetadataを定義する。

完了条件:

- 同一snapshotの比較結果が空になる。
- 差分結果がdeterministicでunit test可能である。

### RG-08 Validationと警告一覧

- [ ] Read前にWriteされていないResourceを検出する。
- [ ] Write後に一度もReadされないResourceを検出する。
- [ ] lifetime外accessと重複ID/nameを検出する。
- [ ] state不一致をRG-04の結果から集約する。
- [ ] Warning一覧から該当nodeへfocusできるようにする。

完了条件:

- 各warningにseverity、message、関連node IDがある。
- False positiveを抑えるためexternal/persistent Resourceを区別する。

### RG-09 Layoutとnavigation改善

- [ ] Passを実行順のlaneへ固定する。
- [ ] Resourceを関連Pass付近へ配置し、link交差を減らす。
- [ ] Texture/Buffer laneを検討する。
- [ ] Mini-map、selected node focus、zoom別detailsを検討する。
- [ ] User移動済みnodeと自動layoutの優先関係を定義する。

完了条件:

- 同一topologyでlayoutがdeterministicかつ安定する。
- 自動layoutがuser操作を毎frame上書きしない。

### RG-10 Authoring/Edit基盤

- [ ] Runtime snapshotとauthoring modelを明確に分離する。
- [ ] Edit command、validation、undo/redoを設計する。
- [ ] Pass追加/削除、Resource接続、順序変更の最小commandを定義する。
- [ ] Serializationとversioningを設計する。
- [ ] 検証済みauthoring dataからruntime graphを再構築する。

完了条件:

- 実行中のruntime graphを直接mutationしない。
- 不正なgraphをruntimeへ適用できない。
- Undo/redoと保存/読込に対するtestがある。

## 共通ルール

- `RenderGraphDocument`はrepository-owned diagnostic/edit境界とする。
- Node identityは完全なpass/resource identityから生成し、表示名だけに依存しない。
- unordered containerのiteration順をlayoutやdumpへ直接使用しない。
- Ping-pong Resourceを単一physical nodeへ統合しない。
- Read-only段階ではgraph mutation APIをviewerへ公開しない。
- 外部library更新、追加vendor、submodule追加は別taskとして明示承認を得る。
- `.vscode/`、`bin/`、`obj/`、`.vs/`、logs、generated captureはcommitしない。

## 現在位置

- [x] 初期text/DOT dump
- [x] `imgui-node-editor` read-only view
- [x] 独立window、最大化/復元、Fit Graph
- [x] Pass/Resource/Texture/Bufferの分類表示
- [x] Ping-pong時のnode位置、寸法、Resource pin layout安定化
- [x] RG-01 選択ノード詳細パネル
- [x] RG-02 検索と表示フィルター
- [x] RG-03 Resource lifetime timeline
- [x] RG-04 State transition診断
- [ ] 次のtask: RG-05 GPU timing表示
