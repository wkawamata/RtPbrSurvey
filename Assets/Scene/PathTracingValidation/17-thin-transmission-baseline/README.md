# PT Thin Transmission baseline

目的: 薄膜の色・不透明度と不透明な枠を確認する。

操作: Scene EditorのHierarchyでPaneを選択。MaterialのTransmission RGBAでRGBとAを変更する。A=0は最大透過、A=1は不透明。IORは反射率に影響する。

確認: 背景模様が板越しに見える。RGB変更が即時反映される。白/赤/緑/青の違い、枠の遮蔽、Undoと保存・再読込みを確認。

制約: 色付きの影、厚み、背景の屈折歪みは未実装。NRD要求時もNativeへフォールバックする。

Top Menu > Saved Scenes > 17-thin-transmission-baseline > Run。

編集はScene Editorで開き、HierarchyのPaneを選択してMaterialを変更する。
Saveは明示操作。元の検証条件を残す場合はSave Asを使用する。

状態: 実装用の検証シーン。目視評価の合格記録ではない。
