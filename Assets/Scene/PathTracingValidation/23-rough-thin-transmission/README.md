# 粗い薄膜透過の比較

左からRoughness 0 / 0.25 / 0.6。IORは全て1.5、透過率1、無色。
左の透過像は鮮明で、中央・右へ進むほど背景格子と反射が広がる。

粗さは薄膜のGGX反射と透過の両方に作用する。巨視的な屈折や厚みはない。
単一散乱GGXのため、高粗さでは失われるenergyを多重散乱で補っていない。
delta lightの薄膜NEE、色付き透過影、volume、alpha maskは対象外。
NRDを要求した場合はNative PTへfallbackする。

`Tests/PathTracing/rough_transmission_fixture.py` で生成した自作geometry。
背景には既存scene20のchecker.gltfを参照する。
