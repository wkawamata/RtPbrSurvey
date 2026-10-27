# PT Thin Transmission angle

目的: 同じ薄膜材質の角度による表面反射の変化を確認する。

構成: 左0度、中央45度、右75度のYaw。全て同じ青色、透過量1、IOR=1.5。

確認: 斜めの板では背景への透過より反射の比率が増える。背面側へカメラを移動しても板が消えない。IOR=1にすると表面反射がなくなる。

制約: 板を傾けると投影面積も変わる。反射像の粗さ・体積屈折は対象外。

Top Menu > Saved Scenes > 18-thin-transmission-angle > Run。

編集はScene Editorで開き、HierarchyのPaneを選択してMaterialを変更する。
Saveは明示操作。元の検証条件を残す場合はSave Asを使用する。

状態: 実装用の検証シーン。目視評価の合格記録ではない。
