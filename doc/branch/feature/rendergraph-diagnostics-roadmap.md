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

- [x] RenderGraph実行ループでpassIndex付きcheckpointを記録し、既存GPU Work Meterからsnapshotを取得する。
- [x] Pass nodeとdetail paneへGPU時間を表示する。
- [x] current、120 frame移動平均、120 frame最大値を切り替えられるようにする。
- [x] frame全体に占める割合を表示する。
- [x] timing未取得時を`GPU N/A`と明確に表示する。

設計上の注意:

- checkpoint名ではなくpassIndexで対応付け、同名Passや表示名差の影響を避ける。
- 計測表示のために新しいGPU waitを追加しない。
- 既存GpuWorkMeterのquery heap/readbackは単一bufferであるため、frame resource単位のreadbackへ分離する改善は別taskとする。

完了条件:

- Timing取得がrendering synchronizationへ影響しない。
- Pass名またはstable IDで安全に対応付けられる。

### RG-06 Ping-pong Resourceの論理グループ表示

- [x] `.0`/`.1`を別nodeとして維持する。
- [x] stable group ID、論理名、physical indexを表すgroup metadataを追加する。
- [x] History ReadとCurrent Writeを固定幅の色付きrole表示で強調する。
- [x] Groupの折り畳みはselection/filter/layoutへの影響が大きいためRG-09以降へ保留する。

完了条件:

- Physical ownershipを失わず、現在のroleが一目で分かる。
- Role交換時もnode位置とBox寸法が安定する。

### RG-07 Snapshot保存と比較

- [x] schema version付きcanonical JSONをsnapshot serialization形式とする。
- [x] 2 snapshot間のnode、link、state、lifetime差分をstable IDで計算する。
- [x] addedを緑、changedを琥珀、removedを赤系の件数表示としてviewerへ反映する。
- [x] label、renderer mode、source commit、feature flagsのmetadataを定義する。

完了条件:

- 同一snapshotの比較結果が空になる。
- 差分結果がdeterministicでunit test可能である。

### RG-08 Validationと警告一覧

- [x] transient ResourceのRead前Write不足を検出する。
- [x] transient Resourceの最終Write後にReadされない状態を検出する。
- [x] lifetime外access、dangling reference、重複ID/nameを検出する。
- [x] state診断はRG-04一覧へ集約し、実barrierとの不一致判定はbarrier event取得後に追加する。
- [x] Warning一覧から該当nodeへfocusできるようにする。

完了条件:

- 各warningにseverity、message、関連node IDがある。
- False positiveを抑えるためexternal/persistent Resourceを区別する。

### RG-09 Layoutとnavigation改善

- [x] Passを実行順のlaneへ固定する。
- [x] Resourceをlifetime中央のPass付近へ配置し、論理グループ順でlink交差を抑える。
- [x] Texture、Buffer、Unknown Resourceごとに間隔を空けた決定的laneを追加する。
- [x] selected node focusを追加する。現libraryにmini-map APIがなく、zoom別detailsはBox安定性を優先して保留する。
- [x] 初回自動layout後はUser移動を優先し、明示的な`Reset Saved Layout`時だけ保存座標を破棄して再配置する。

完了条件:

- 同一topologyでlayoutがdeterministicかつ安定する。
- 自動layoutがuser操作を毎frame上書きしない。

### RG-10 Authoring/Edit基盤

- [x] Runtime snapshotと独立した`RenderGraphAuthoringDocument`を定義する。
- [x] Candidate validation後だけ適用するEdit commandとundo/redo historyを実装する。
- [x] Pass/Resource追加・削除、接続・切断、Pass順序変更commandを定義する。
- [x] schema version付きcanonical JSON serializationを実装する。
- [x] 検証済みauthoring dataから適用前preview `RenderGraphDocument`を再構築する。

初期基盤の境界:

- Viewerは引き続きread-onlyで、実行中runtime graphへのmutation APIを持たない。
- Pipeline、descriptor、operation bindingを含む実runtime適用は、preview validationと明示的なapply transactionを設計した後に追加する。

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
- [x] RG-05 GPU timing表示
- [x] RG-06 Ping-pong Resourceの論理グループ表示
- [x] RG-07 Snapshot保存と比較
- [x] RG-08 Validationと警告一覧
- [x] RG-09 Layoutとnavigation改善
- [x] RG-10 Authoring/Edit基盤
- [x] RenderGraph診断機能ロードマップ初期版完了
- [x] GPU timestamp query/readbackをframe resource単位へ分離

## Follow-up: GPU timing readback安全化

- [x] 各frame slotが専用のquery heap、readback buffer、query indexを所有する。
- [x] `MoveToNextFrame`で既存fenceの完了が保証されたframe slotだけを読み戻す。
- [x] UI表示用checkpointを記録中のframe resourceから独立した完了済みsnapshotへコピーする。
- [x] timing取得のための追加GPU waitを導入しない。
- [x] Debug x64 buildでコンパイルとリンクを確認する。

この変更により、直前にsubmitした未完了queryをCPUからMapする可能性をなくす。UIには最新submit frameではなく、最後にfence完了を確認できたframeのtimingを表示する。

## Follow-up: 実Resource Barrier診断

- [x] graph実行時に発行したtransitionをpass index、resource名、before/after state付きで記録する。
- [x] 記録したbarrier eventと`RenderGraphDocument`の要求transitionをdeterministicに照合する。
- [x] missing、unexpected、state mismatchをunit testで検証する。
- [x] 照合結果を既存State Diagnostics一覧へ統合する。
- [x] 実barrierの一致・不一致表示を統合画面確認リストへ追加する。

## 統合画面確認

- [x] RG-01: Pass/Resource選択とdetail paneの内容を確認する。
- [x] RG-02: 検索、種別filter、lifetime filter、接続node強調を確認する。
- [x] RG-03: Resource lifetime timeline、`.0`/`.1`隣接表示、selection同期を確認する。
- [x] RG-04: state transitionとUAV barrier候補のedge色・一覧を確認する。
- [x] State Diagnosticsで実barrierのMissing、Unexpected、State mismatch表示を確認する。
- [x] RG-05: GPU current/120 frame平均/最大とframe比率を確認する。
- [x] Timing未取得時の`GPU N/A`表示を確認する。
- [x] GPU timingがframe slot再利用後も連続更新され、値が不自然に欠落・破損しないことを確認する。
- [x] RG-06: Ping-pong Resourceのphysical indexとHistory Read/Current Write交換を確認する。
- [x] RG-07: baseline設定・解除、added/removed/changed件数と色分けを確認する。
- [x] RG-08: Validation一覧、severity表示、該当nodeへのfocusを確認する。
- [x] RG-09: Focus Selected、Reset Saved Layout、Texture/Buffer/Unknown laneを確認する。
- [x] RenderGraph windowの通常サイズ、最大化、復元でUI崩れがないことを確認する。
- [x] 同一topologyでnode位置・Box寸法が安定し、ping-pong時はlink/roleだけが切り替わることを確認する。
- [x] Debug x64を10秒間自動実行し、D3D12 Debug Layer logが空であることを確認する。
- [x] 自動captureまたは手動screenshotを保存し、主要表示の確認記録を残す。

### 2026-08-29 主観評価メモ

- 検索欄で`ReflectionHistoryDepth`を絞り込み、該当Resourceの強調を確認した。種別・lifetime・接続nodeの全組み合わせは未確認のためRG-02全体は継続する。
- `ReflectionHistoryDepth.0`のdetail paneにTexture、Persistent、pass range、logical group、physical index、current role、pass usageが表示された。
- 同じnode位置とBox寸法のまま`Current Write`と`History Read`が交換し、baseline比較ではnode変更とlink交換が表示された。
- GPU Average値は各Pass nodeに表示され、Current、Maxと連続更新も確認した。`GPU N/A`表示のみ継続する。
- Validationは`No validation messages.`だったため、warning発生時のseverityとfocus動作は未確認。
- `Focus Selected`と検索結果focusは、単一nodeを画面いっぱいに拡大せず、現在のzoomを維持して中央へ移動するようにした。
- 検索、検索結果focus、接続node強調、Texture種別、Persistent lifetime filterで、非該当nodeが薄くなり、link endpointとselectionが維持されることを確認した。
- GPU timingのCurrent、120 frame平均、120 frame最大を切り替え、Pass表示が継続更新されることを確認した。Timing未取得時の`GPU N/A`も別途確認した。
- RG-08はvalidation message生成をunit testで確認した。正常runtime graphには警告がないため偽警告は注入せず、警告focusと共通のselection/navigation経路を検索focusの実画面操作で確認した。
- 最大化したRenderGraphを含む手動screenshotを`C:\Users\wkawa\OneDrive\画像\Screenshots\RtPbrSurvey-RenderGraph-2026-08-30.png`へ保存した。検証用生成物のためrepositoryには追加しない。
- 呼び出し側のtimingを一時的に未取得状態としてDebug buildし、ClearとDepth PrePassのPass nodeに`GPU N/A`が表示されることを実画面で確認した。一時変更は確認直後に戻した。

確認中に見つかった問題候補:

- 通常のScene SelectからDamagedHelmetを`Load Scene`した際の`assert(loaded)`は、glTF相対pathがprocessのcurrent directoryに依存していたことが原因だった。exe directory基準で解決するよう修正し、exe直接起動からの`Load Scene`で正常表示を確認した。
- State Diagnosticsの`2 runtime mismatches`は、前フレームのbarrier eventを現在フレームのping-pong graphと比較した誤検出だった。同じフレームでcaptureしたdocumentとeventを保持して照合するよう修正し、DamagedHelmetで`19 required barriers / 0 runtime mismatches`を確認した。

## 後続タスク

- [x] `Open RenderGraph Window`を`Close Scene`ボタンの右側へ、少し余白を空けて配置する。
- [x] RenderGraphノードの手動配置を`%APPDATA%\\RtPbrSurvey\\rendergraph_node_editor.json`へ自動保存し、次回起動時に復元する。
