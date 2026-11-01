# 金属度と薄膜透過

左からMetallic 0 / 0.5 / 1。Transmissionは全て1、Roughnessは0.25。
金属度が高いほど背景格子の透過が減り、右は金属反射のみになる。

中間値は既存不透明・薄膜誘電体・金属反射の混合として評価する。
透過率0または金属度1では透過用乱数を消費せず、既存不透明BSDFを維持する。
MRテクスチャのGは粗さ、Bは金属度をlinearとして各factorへ掛ける。
volume、alpha mask、色付き透過影は未対応。NRD要求時はNative fallback。

自作geometryは `Tests/PathTracing/metallic_transmission_fixture.py` で生成する。
背景はscene20のchecker.gltfを参照する。
