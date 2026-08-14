# imgui-node-editor の導入情報

この directory は [`thedmd/imgui-node-editor`](https://github.com/thedmd/imgui-node-editor) の source を vendor したものである。

- Source commit：`55a7dbf4b517b5e809b372ba39153fe20bad39ad`
- Upstream base：`021aa0ea4da13fed864bafb2a92d4c5205076866`
- Compatibility changes：upstream PR #339 head `ca3d8d2f433ae9e3e3cca7ca609fbde09fbf533d` と PR #335 の `ImVec2` operator guard を含む。
- 選定理由：RtPbrSurvey が使用する Dear ImGui 1.92.8 の `AddRect()` / `PathStroke()` argument order と `ImVec2` operator 定義に対応するため。
- License：同 directory の `LICENSE`（MIT License）。
- Local source modification：なし。

Examples、documentation、CMake support file は含めず、library 本体の compile に必要な root source/header のみを保持する。
