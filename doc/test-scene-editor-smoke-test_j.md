# Test Scene Editor スモークテスト

## 目的

Test Scene Editor で作成した保存済みシーンが、再読込と Evaluation Case の復元後にも同じ検証条件を再現できることを確認する。

## 自動確認

次の CTest を Debug 構成で実行する。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" `
  --test-dir build\scene-document-tests -C Debug --output-on-failure `
  -R "RtPbrSurvey\.(SceneDocument|SceneGraph|SceneEditorSession|EvaluationState)"
```

確認対象:

- Scene JSON の読書き、参照と値の検証
- Primitive / glTF を含む SceneDocument の構築
- Scene Editor の Undo、階層、Asset共有、コピー・貼り付け
- Evaluation State の file-backed Scene Reference 保存

## 実機確認手順

1. TopMenu で `SceneEditor` を開き、`New Creation` を選ぶ。
2. Cube または Plane を追加し、`glTF Asset Path` に既存の glTF を指定して `Add glTF` を実行する。
3. Node 名、Transform、Primitive Shape、Material、Camera、Environment を編集する。
4. `Save As` で `Assets/Scenes/<検証名>/scene.json` へ保存する。
5. `Back to TopMenu` の後、SceneEditor の `Saved Scenes` から同じシーンを Load する。
6. Hierarchy、Asset Path、Material、Camera、Environment、Render Preset が保存前と一致することを確認する。
7. 実行中の Debug UI で Evaluation Case を新規作成して保存する。
8. 別の Scene を開いた後、保存した Evaluation Case の `Open Scene` を実行する。
9. Case が Test Scene、Render Preset、Camera、ROI、Renderer 設定を復元することを確認する。

## 合格条件

- Load と Evaluation Case 復元でエラー表示がない。
- 保存した Scene ID と Asset / Material / Node 参照が維持される。
- Case 復元時に Scene Document と Render Preset が読み直され、Running モードになる。
- Debug build の D3D12 Debug Layer に新たな `[ERROR]` が出ない。

## 現在の実行結果

2026-09-19 に focused CTest を実行し、SceneDocument、SceneDocumentBuilder、SceneEditorSession、EvaluationState の 4 件が成功した。

この自動化環境では native application の UI 操作対象と GPU キャプチャ画像を取得できない。CLI の `-SceneFile` 自動キャプチャは終了コード 0 で終了したが PNG を出力しなかったため、実機手順の完了判定には含めない。
