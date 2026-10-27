# PT Thin Transmission layers

目的: 色付き薄膜の重なりと、間の不透明物による遮蔽を確認する。

構成: 手前は赤系、奥は青系の板。IOR=1で反射を止め、色の乗算を見やすくしている。板の間に灰色のCubeがある。

確認: 重なりでは色が掛け合わされる。Cubeの背後にある板や模様はCubeを透過しない。カメラと板を動かして遮蔽が更新される。IOR=1.5へ変更すると反射が加わる。

制約: 光源から床へ色が投影されるテストではない。2枚と背景までの経路に十分な8バウンスを設定。

Top Menu > Saved Scenes > 19-thin-transmission-layers > Run。

編集はScene Editorで開き、HierarchyのPaneを選択してMaterialを変更する。
Saveは明示操作。元の検証条件を残す場合はSave Asを使用する。

状態: 実装用の検証シーン。目視評価の合格記録ではない。
