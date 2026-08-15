# RenderGraph ノードダンプ調査

## 目的

この branch では、RenderGraph の診断と可視化に向けた、実用上最小の第一歩を定義する。当面の目的は、RenderGraph の実行、descriptor 管理、PathTracing、temporal upscaler 統合を変更せず、現在の pass/resource 関係を確認できるようにすることである。

現在の frame graph model には、診断表示に必要なデータがすでに存在する。

- `Engine::RenderPassGraph` は順序付き pass list を保持する。
- 各 `Engine::RenderPass` は、名前付きの read/write resource usage と `D3D12_RESOURCE_STATES` を公開する。
- `AnalyzeResourceLifetimes()` は最初と最後の pass index を算出する。
- `RenderTextureSpec` と transient resource registry により、persistent resource と transient resource を区別できる。

このため、最初の実装ではライブラリなしの診断表現が実現可能である。interactive UI library の選定は完了しており、将来の viewer/editor には `imgui-node-editor` / `ax::NodeEditor` を使用する。

## 候補比較

| 候補 | License | 統合方法と依存関係 | Maintenance 状況（2026-07-24 確認） | Build への影響 | 初期版との適合性 |
|---|---|---|---|---|---|
| [`imgui-node-editor` / `ax::NodeEditor`](https://github.com/thedmd/imgui-node-editor) | [MIT](https://github.com/thedmd/imgui-node-editor/blob/master/LICENSE) | Dear ImGui と C++14 が必要。upstream は root directory にある一連の source を project にコピーする方法を案内している。実用的な統合では editor implementation/API、canvas/math helper、小規模な JSON support が含まれ、単一 translation unit より導入範囲が広い。ImGui 経由で描画するため別の DX12 backend は不要。 | 最新 tag release `v0.9.3` は ImGui 1.92.8 より古い。`master` `021aa0e` に対する PR #339 の draw API argument order 修正と PR #335 の `ImVec2` operator guard を含む `55a7dbf` を pin した。 | Vendored source と license 表示、MSBuild/CMake compile entry、editor context lifecycle、安定した node/pin ID、warning/ImGui compatibility check を追加した。 | Viewer/editor 用として選定し、read-only node view まで導入済み。充実した navigation、selection、grouping、拡張可能な node/pin 表示が、診断から編集へ進む方針に合う。 |
| [`imnodes`](https://github.com/Nelarius/imnodes) | [MIT](https://github.com/Nelarius/imnodes/blob/master/LICENSE.md) | Upstream は Dear ImGui 以外に依存しないとしており、`imnodes.cpp`、`imnodes.h`、`imnodes_internal.h` の 3 file で配布する。独自の DX12 backend は持たず、既存の ImGui renderer を使う。graph state、ID、表示内容は application 側が所有する。 | GitHub 上の最新 tag release は `0.5`（2022-03-09）。API と source の規模が小さく導入 cost は低いが、release が古いため、この repository の ImGui revision との compatibility/maintenance risk を検証する必要がある。 | 3 個の vendored file と license 表示、1 個の compile entry、include 設定、context lifecycle、安定 ID、compatibility test が必要。`imgui-node-editor` より統合 cost は低い。 | 不採用。小規模である点は魅力だが、最初の viewer に採用すると、編集機能と高度な navigation を追加する段階で library 移行が発生する可能性が高い。 |
| 外部ライブラリなし：ImGui table/text tree と DOT text export | Repository 内の code のみ。DOT text の出力は Graphviz の link や vendor を必要としない。開発者が描画結果を必要とする場合のみ、任意の外部 viewer として Graphviz を使用する。 | 既存の ImGui 統合を in-app view に使用するか、plain DOT syntax を出力する。新しい runtime、package、submodule、backend、third-party source は不要。Graphviz が未 install でも `.dot` file を text として確認できる。 | RenderGraph data model とともに repository 内で保守する。DOT は安定した graph 記述言語であり、exporter に必要なのは小さな escape 処理と deterministic な ID/order のみ。 | 小規模な diagnostic helper と、任意の Debug UI entry を追加する。project 全体の dependency 設定は不要。test では完全一致または意味上重要な断片を検証できる。 | 初期版として推奨。canvas library を導入する前に、診断 schema と workflow を検証できる。 |

## License と repository への導入

両 node editor 候補は MIT License であり、copyright notice と permission notice を保持すれば再配布できる。選定した `imgui-node-editor` は `third_party/imgui-node-editor` に vendor し、同 directory に upstream `LICENSE` と `README-RtPbrSurvey.md` の commit/provenance 情報を保持する。Submodule や download/package script は追加しない。

推奨する DOT exporter は、[DOT language](https://graphviz.org/doc/info/lang.html) に従う text を出力するだけとする。Graphviz の起動、同梱、link は行わない。開発者は必要に応じて、application や build とは独立した外部 tool として Graphviz の `dot` を使用できる。

## 決定と推奨案

Interactive viewer/editor には `imgui-node-editor` を使用する。Library-free dump を先に完成させた後、同じ document model を使う read-only node view として library を統合した。

最初に、1 個の read-only diagnostic snapshot と、それを使用する複数の出力を定義する。

1. Log、test、issue report に使用できる deterministic な text dump。
2. Offline graph visualization に使用できる deterministic な DOT export。
3. 必要であれば、同じ snapshot を表示する小規模な ImGui table/tree。

Snapshot は ImGui、`imgui-node-editor`、DOT formatting から独立させる。この分離により graph semantics と UI library の結合を避け、dump、read-only node view、将来の editor で共通の安定した model を使用できる。

## Model architecture

Node 情報は実行用 RenderGraph から分離して保持する。ただし、無関係な第二の source of truth として管理してはならない。

Repository が所有する中間 model として、仮称 `RenderGraphDocument` を導入し、次の 3 layer に分ける。

1. `RenderPassGraph` は実行用 graph の authoritative data であり続ける。
2. Builder が runtime graph と registry から `RenderGraphDocument` を生成する。
3. Dump exporter と `imgui-node-editor` adapter が document を参照する。

Document では repository が所有する ID と型を使用する。

- `DocumentNodeId`、`DocumentPinId`、`DocumentLinkId` は安定した logical ID とする。
- Pass node と resource node は semantic data を保持し、`ax::NodeEditor::NodeId` は保持しない。
- Pin は方向、access、resource state、connection policy を表す。
- Link は pass/resource 関係を表し、将来は編集可能な connection に対応する。
- Position、zoom、selection、collapsed group などの表示専用 state は、別の `RenderGraphEditorViewState` に置く。

`imgui-node-editor` layer が document ID を `ax::NodeEditor` ID に変換する。RenderGraph の runtime header や document model に `ax::NodeEditor` の型を露出させない。この adapter 境界により、`imgui-node-editor` を採用する方針を維持しながら、library version や統合方法を変更しても graph semantics を書き直さずに済む。

### Read と edit の flow

最初の version は一方向とする。

`RenderPassGraph -> RenderGraphDocument -> text/DOT dump`

その後、read-only node view を追加する。

`RenderGraphDocument + RenderGraphEditorViewState -> imgui-node-editor`

将来の編集機能では、UI callback から runtime graph を直接変更しない。Add/remove pass、connect/disconnect resource、pass property update などを表す、repository 所有の明示的な command を生成する。Command を検証して authoring model に適用し、そこから実行用 `RenderPassGraph` を再構築して検証する。検証に失敗した場合は、最後に正常だった runtime graph を維持する。

この command 境界は重要である。現在の `RenderPassGraph` は手続き的に構築され、実行用 object を含む。その vector を editor database として直接扱うと、UI 操作が runtime safety に結合し、undo/redo、validation、serialization、error report の実装が難しくなる。

### Ownership rule

- Runtime semantics：`RenderPassGraph` と各 registry。
- Diagnostic/editor semantics：`RenderGraphDocument`。
- Canvas state：`RenderGraphEditorViewState`。
- Library 固有の context、ID、drawing、callback：`ImguiNodeEditorRenderGraphView`。
- 将来の変更操作：repository 所有の editor command と authoring model。library callback から runtime container を直接変更しない。

## 最初の実装範囲

最初の code change は library-free、read-only の範囲に限定する。

### 実装状況

初期実装として `Engine/FrameGraph/RenderGraphDocument.h/.cpp` を追加した。

- `RenderGraphDocument` が pass/resource node、pin、link を repository-owned ID とともに保持する。
- `BuildRenderGraphDocument()` が現在の ordered pass list から document を生成する。
- Resource metadata map を介して transient/persistent classification を付加できる。
- `DumpRenderGraphDocumentText()` が pass 順の deterministic text dump を生成する。
- `DumpRenderGraphDocumentDot()` が read/write direction と resource state を含む deterministic DOT を生成する。
- `FormatD3D12ResourceStates()` が symbolic state name と未知 flag の hexadecimal fallback を提供する。
- `Tests/RenderGraphDocumentTests.cpp` が topology、ID uniqueness、deterministic output、state formatting を検証する。

Runtime integration として、次の read-only entry point を追加した。

- `RtPbrSurveyEngine::CaptureRenderGraphDocument()` が構築済み runtime graph を snapshot 化する。
- Engine が resource registry の `persistent` flag を document metadata の transient/persistent classification に変換する。
- `SceneRenderer::CaptureRenderGraphDocument()` が同じ snapshot を host-facing API として公開する。
- `SceneRendererDebugUi::DrawRenderGraphDiagnostics()` が pass/resource/link count、text/DOT preview、clipboard copy を提供する。
- Standalone app の Debug window と host-facing `SceneRendererDebugUi::Draw()` の両方で、同じ diagnostics UI を再利用する。

UI は section を展開したときだけ snapshot と dump を生成する。`Nodes` view を選択すると `RenderGraphNodeEditorView` が document ID を `ax::NodeEditor` ID に変換し、pass/resource node、read/write pin、link を描画する。Scroll 可能な Debug UI の末尾でも canvas が潰れないよう、node editor view は 420 pixel の表示高を確保する。初期 position は lifetime/pass index から一度だけ設定し、新しい node を配置した frame では graph 全体へ自動で fit する。その後の user navigation と node movement は view state として editor context が保持され、`Fit Graph` button を押した場合だけ全体表示へ戻す。Library の settings file は無効化しており、local JSON は生成しない。Graph mutation と file write はまだ追加していない。

### Diagnostic snapshot

`BuildRenderPasses()` と validation の後に graph を取得する。Mutable な runtime object を外部公開せず、安定した diagnostic record を生成する。

- Pass：ordered index、表示名、active/included status、解決可能な場合は pipeline key/name と operation key/name。
- Resource：名前、first/last pass index、registry に存在する場合は transient/persistent classification、既知の場合は initial/current state。
- Edge：pass index、resource name、access type（`read` または `write`）、要求する `D3D12_RESOURCE_STATES`。

現在の graph には、`BuildRenderPasses()` で選択された pass だけが含まれる。そのため、初期版の `active` は built graph に存在することを意味する。無効な候補 pass の表示には別の authoring registry が必要となるため、初期範囲には含めない。

### Text dump

Pass 順が deterministic な report を出力する。各 pass は read と write を列挙し、各 resource line には requested state と、判明している場合は lifetime と transient/persistent classification を含める。可能な場合は symbolic state name を使い、組み合わせや未知の flag には hexadecimal fallback を残す。

### DOT export

有向 bipartite graph を出力する。

- Pass node と resource node は異なる shape/color を使用する。
- `resource -> pass` は read を表す。
- `pass -> resource` は write を表す。
- Edge label に access と requested state を含める。
- Resource label に、判明している場合は lifetime と transient/persistent classification を含める。

Exporter は identifier と label を quote/escape し、name を DOT ID として直接使わず、生成した安定 ID を使う。Review しやすい diff を得るため、出力順を deterministic にする。

### Debug UI entry

In-app entry を追加する場合は、既存 Debug UI の小規模な read-only window または collapsing section とする。Filter と copy/export control は追加してよいが、graph mutation、pass reorder、resource edit、node-link creation は許可しない。

### 明示的に延期する項目

- Interactive graph editing。
- Application 内の自動 graph layout。
- ユーザーが設定した node position の永続化。
- RenderGraph scheduling または lifetime policy の変更。
- Descriptor heap の変更。
- PathTracing、Streamline、DLSS、reflection contract、shadow validation に関する作業。

## 将来の node editor 導入手順

実際の graph に dump format を使用した後、次の順で進める。

1. Deterministic な text/DOT dump を通して `RenderGraphDocument` を実装し、test する。
2. Graph size、filtering、grouping、selection synchronization、search、state transition 表示、layout persistence について、実際の usability requirement を記録する。
3. [x] `imgui-node-editor` を ImGui 1.92.8 対応 commit に pin し、MIT license と出典を保持する。
4. [x] 既存 document に対する adapter と read-only view を追加する。UI の都合で document schema を分岐させない。
5. [ ] Mutation を有効にする前に、repository 所有の edit command、validation、undo/redo、serialization を追加する。
6. [ ] 実行中の runtime graph を直接編集せず、検証済み authoring data から executable graph を再構築する。

## Build integration の評価

Application は `Ui/ImGuiSystem` で Dear ImGui の初期化と Win32/DX12 backend をすでに所有している。`imgui-node-editor` は既存 ImGui context の上に配置し、別の graphics backend は追加しない。ただし、独自 context の lifecycle hook と、明示的な `.vcxproj` source entry が必要になる。

Library-free 版を実装する場合は、repository 所有の C++ file だけを追加する。この project は `RtPbrSurvey.vcxproj` に source を明示的に列挙しているため、将来 helper implementation を追加する際は project file にも追加する必要がある。Documentation のみの調査では compile input は変化しない。

## Validation

- Debug x64 の `RtPbrSurvey.vcxproj` build：成功。既存 vcpkg target の duplicate import warning が 1 件、compile/link error は 0 件。
- `RenderGraphDocumentTests` の単独 build と実行：成功、exit code 0。
- Runtime/Debug UI integration 後の Debug x64 build：成功。既存 warning 1 件、compile/link error は 0 件。
- Runtime/Debug UI integration 後の `RenderGraphDocumentTests` 再実行：成功、exit code 0。
- Vendored `imgui-node-editor` と read-only node view を含む Debug x64 build：成功。既存 warning 1 件、compile/link error は 0 件。
- Node view integration 後の `RenderGraphDocumentTests` 再実行：成功、exit code 0。
- CLI runtime check：`-AutoSelectGltfDamagedHelmet -CaptureAfterFrames 30 -ExitAfterCapture` で正常終了し、exit code 0 と PNG capture の生成を確認した。Debug Layer log には buffer の initial state が無視される既存 warning が 2 件あり、`[ERROR]` は 0 件。
- Node view の visual runtime check：DamagedHelmet の実行中 graph（11 passes、21 resources、51 links）で `Nodes` canvas、pass node、state 付き pin、link の描画を確認した。初回確認で canvas 高が不足したため 420 pixel の固定高を追加し、修正後に再確認した。
- 最新 `origin/main` への rebase 後に `Fit Graph` と新規 node 配置時の自動 fit を追加し、Debug x64 build と `RenderGraphDocumentTests` は成功した。追加の visual check は Computer Use の app approval timeout により未実施。
- 通常の CMake test configure：`tinygltf v3.0.0` の upstream download hash 不一致により停止。RenderGraph code の compile 前に dependency restore で失敗しているため、代わりに repository の既存 local dependency を使う一時 MSBuild project で同じ test source を compile/run した。一時 project と build output は `build/` 以下にあり、commit 対象外。

## 参照先

- `imgui-node-editor` repository、dependency、distribution guidance、feature、release metadata：<https://github.com/thedmd/imgui-node-editor>
- `imgui-node-editor` license：<https://github.com/thedmd/imgui-node-editor/blob/master/LICENSE>
- `imnodes` repository、3 file distribution、API overview、release metadata：<https://github.com/Nelarius/imnodes>
- `imnodes` license：<https://github.com/Nelarius/imnodes/blob/master/LICENSE.md>
- Graphviz DOT language specification：<https://graphviz.org/doc/info/lang.html>
- Graphviz `dot` layout documentation：<https://graphviz.org/docs/layouts/dot/>
