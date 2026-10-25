# 粗さテクスチャと薄膜透過

左: G x 1.0。中央: G x 0.5。右: 定数Roughness 0.5。
左と中央には、粗さの異なる4領域がある。背景格子のぼけ方を比較する。

- Gは0/64/128/255。linearでroughnessFactorに乗算する。
- R/B/Aは異なる値を入れてあるが、粗さには使わない。sRGB変換もしない。
- metallicFactor=0なので、画像Bが非ゼロでも非金属。
- UV下辺がv=0のfixture。PNGと画面の上下が異なる場合がある。
- TEXCOORD_0、既存共有sampler、mip 0。変換や別UV集合は未対応。
- volume、alpha mask、metallicとの透過混合、色付き透過影は未対応。
- NRD要求時はNative PTへfallback。

自作geometryと小さなPNGは `Tests/PathTracing/roughness_texture_fixture.py` で生成。
背景はscene20のchecker.gltfを参照する。
