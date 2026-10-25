# Alpha MASKと薄膜透過

左はOPAQUE、中央はMASK、右はMASKと薄膜透過の組合せ。
RGBA画像のRGBは白、Aは0/64/128/255。cutoff=0.5。

- 左はalphaを無視し、全面が青い表面になる。
- 中央はalphaがcutoff未満の領域で表面そのものがなくなり、背景が見える。
- 右も同じ領域が切り抜かれるが、残った領域は青い光学透過になる。
- alphaはlinear。baseColorFactor.aを掛けてからcutoff以上か比較する。
- Native PT限定。BLEND、volume、色付き透過影は未対応。
- MASKのNRD品質は未認定のため、要求時は理由を表示してNative fallback。

自作geometryは `Tests/PathTracing/alpha_mask_fixture.py` で生成。
背景はscene20のchecker.gltfを参照する。
