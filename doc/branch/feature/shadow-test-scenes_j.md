# feature/shadow-test-scenes

## RayQuery Shadow メモ

RayQuery shadow の検証用シーンは既に存在する。

- `Shadow Test: Ground + Cubes`
- `Shadow Test: Animated Shadow Grid`
- `Shadow Test: Contact Shadow Test`
- `Shadow Test: Occluder Wall Test`

新しい failure mode が既存シーンで切り分けられない場合を除き、専用 scene は追加しない。まずは UI preset、camera default、比較 checklist を整えて、同じ scene で branch 間比較しやすくする。

## Scene Usage

- `Shadow Test: Ground + Cubes`: shadow direction、bias、normal bias、peter-panning、light size、soft shadow の安定性を見る静的な基準 scene。
- `Shadow Test: Animated Shadow Grid`: TLAS rebuild timing、`prevWorld` / motion vector、moving occluder、pause behavior を見る moving-object scene。
- `Shadow Test: Contact Shadow Test`: contact 付近の acne と detached contact の tuning を見る scene。
- `Shadow Test: Occluder Wall Test`: blocker / receiver の分離、missed occluder、back-face culling mistake、long ray behavior を見る scene。

推奨 debug view sequence:

1. まず `ShadowMask` で、direct-light shading を外した binary / softened mask を見る。
2. `Lit` に戻し、direct light の方向と mask の方向が一致しているか見る。
3. mask shape が別 object や stale transform に見える場合は `TlasDebug` を見る。

## UI Presets

`RayQuery Shadow` debug UI には比較用 preset がある。

- `Hard Ref`: 1 sample の hard shadow baseline。soft shadow 調整の前に使う。
- `Low Bias`: normal bias を下げ、self-intersection acne と contact sensitivity を露出させる。
- `Soft Compare`: 通常比較用の moderate soft shadow。
- `Wide Soft`: light size と sample count を大きくし、penumbra と noise を stress する。

preset 適用後は、検証したい観点の slider だけを動かす。screenshot やメモには scene、render view、preset、camera position を一緒に残す。

## Comparison Checklist

- Bias / normal bias: flat receiver の acne と、cube foot / sphere contact の peter-panning を比較する。
- Soft shadow: `Hard Ref` と `Soft Compare` を `ShadowMask` / `Lit` で比較し、edge が方向反転や blocker loss なしに soft になるか見る。
- Light size: `Light Angular Radius` を上げ、penumbra width が予測どおり広がり、contact が不自然に浮かないか見る。
- Moving object: `Shadow Test: Animated Shadow Grid` で animated cube の rotation / bounce が毎 frame TLAS と ShadowMask に反映されるか見る。
- Pause behavior: `Shadow Test: Animated Shadow Grid` で Space pause し、cube orientation、TLAS debug、ShadowMask が snap せず止まるか見る。
- Back-face culling: light / camera angle を変えても cube shadow face が欠けないか見る。
- Ray distance: `Ray TMax` は far blocker 問題の切り分けにだけ使う。transform や culling 修正の代わりにしない。

## Light Direction

`lightDirection` は surface-to-light 方向として扱う。

ShadowMask 生成と LightPass の direct lighting は同じ向きに揃える。

- `shaders_RayQueryShadow.hlsl`: `rayDir = normalize(lightDirection)`
- `shaders_LightPass.hlsl`: `lightDir = normalize(lightDirection)`

片方だけ `-lightDirection` を使うと、ShadowMask 単体では正しく見えても、最終描画の光と影の向きがずれる。

## RayQuery Culling

現在の shadow ray では `RAY_FLAG_CULL_BACK_FACING_TRIANGLES` を使わない。

shadow ray は binary occlusion test なので、back face も遮蔽物として有効に扱う。back-face culling を有効にすると、Cube の面ごとに hit する面と抜ける面が分かれ、影が部分的に欠ける。

期待する形:

```hlsl
RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
```

## TLAS Instance Transform

描画 shader は `InstanceData::world` を直接使う。Scene 側ではこの行列を `XMMatrixTranspose(M)` として保存している。

`D3D12_RAYTRACING_INSTANCE_DESC::Transform` に詰めるときも、shader が見る行列規約と揃える。つまり `InstanceData::world` の先頭 3 行をそのまま使う。

壊れていた実装では、一度 untransposed matrix に戻すような詰め方をしていた。平行移動だけの scene では目立ちにくいが、回転する Cube では ShadowMask に「別の Cube が投影された」ような模様が出る。

## Bias Tuning

ShadowMask が構造的におかしい場合、最初に normal bias を調整しない。

bias は self-intersection acne と peter-panning の調整用。mask の向きがおかしい、別オブジェクトが投影されたように見える、という場合は先に以下を見る。

1. ShadowMask と LightPass の light direction が一致しているか。
2. RayQuery に back-face culling を入れていないか。
3. TLAS instance transform の詰め方が shader 側と一致しているか。
4. animated instance に対して TLAS rebuild が追従しているか。

現在の基準値:

```text
Normal Bias = 0.01
Ray TMin = 0.001
```

## Animated Pause

Pause は現在の accumulated animation time を止めるだけにする。回転項に pause 用 speed を掛けてはいけない。

問題のある形:

```cpp
const float speed = context.isPlaying ? 1.0f : 0.0f;
const float rotY = m_accumTime * rotSpeed * speed + phase;
```

正しい形:

```cpp
const float rotY = m_accumTime * rotSpeed + phase;
```

`m_accumTime` は Pause 中に進まない。さらに `speed = 0` を掛けると、現在姿勢ではなく `phase` だけの姿勢に戻ってしまう。

## 期待する結果

- ShadowMask の方向と最終 direct lighting が一致する。
- Cube の shadow face が RayQuery back-face culling によって欠けない。
- Animated cube の回転が TLAS / ShadowMask に反映される。
- Space で Pause しても Cube の姿勢が変化しない。
- すべての shadow validation scene が一貫して見える。

## Branch 完了条件

既存の4シーンを RayQuery shadow の安定した検証 suite とする。scene selector では `Shadow Test` にまとめ、`RayQuery Shadow` panel では hard、low-bias、soft-shadow を再現可能な preset として提供する。現在対象としている bias、light size、occluder、moving object の検証には追加 scene を必要としない。

最終確認は Debug x64 build の成功と、4シーンを順番に開く手動起動確認で行う。shader compile の成功だけでは shadow quality を判定できないため、目視確認結果は別途記録する。

2026-08-11 の検証結果:

- MSBuild Debug x64: 成功。
- CMake SDK-free Debug build: 成功。
- CTest: 9/9 成功。
- Debug executable: 6秒間正常に起動を継続し、D3D12 warning / error は0件。
- 4シーンの目視比較: GPU環境依存の手動 acceptance として残す。
