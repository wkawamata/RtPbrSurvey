# Hybrid Diffuse IBL 環境可視性の試作設計

作成日: 2026-10-31
状態: 設計。描画への組込みは未実施。
根拠: [IBL遮蔽の切り分け結果](hybrid-diffuse-ibl-visibility.md)。

## 目的

閉じたCornellの内部までDiffuse IBLで照らされる問題を改善する。
カメラから見える一次表面に、環境へ抜ける方向の情報を与える。
壁からの反射光や色移りは別の間接Diffuse機能として扱う。

試作は既定OFFとし、既存の直接光、Shadow Mask、Specular IBL、Hybrid Reflection、
Native PT、RR/NRDに使う値は変更しない。IBL全体や最終画像を暗くする実装にはしない。

## 最初に試す方式

GBufferのworld positionとnormalから、cosine-weighted hemisphereへ決定的な16本のrayを飛ばす。
遮蔽物に当たらなかったrayの割合を、スカラーの環境可視率 `V` とする。

```text
V(x,n) = (1/pi) * integral_hemisphere visibility(x,w) * max(dot(n,w),0) dw
V_estimate = number_of_misses / sample_count

diffuseIbl = existingDiffuseIbl * lerp(1, V_estimate, strength)
```

cosine分布でsampleするため、missの平均へcosや1/piを再度掛けない。
`existingDiffuseIbl`は現在のirradiance、albedo、metallic、material AO、IBL intensityを含む。
既存material AOと環境可視率は別の入力として保持する。AO付き実assetでは二重に暗くなる
近似があり得るため、AO=1の制御fixtureで先に検証し、その後実assetを観察する。

一様な環境ではこの係数で遮蔽付きirradianceを表現できる。一般の環境では
`integral(L * V * cos) != integral(L * cos) * mean_cos(V)` なので、
太陽・窓方向だけが塞がれる場合の照明分布は正しく再現できない。
試作結果を根拠なく全HDRI向けの正確なIBL遮蔽と呼ばない。

## 既存コードとの接続

| 境界 | 試作の変更 |
| --- | --- |
| 新規 `Renderer/DiffuseEnvironmentVisibilityPass.*` | compute dispatch、専用root signature/PSO |
| 新規 `Shaders/shaders_DiffuseEnvironmentVisibility.hlsl` | hemisphere sampling、RayQuery、mask書込み |
| `RtPbrSurveyEnginePasses.cpp` | GBuffer/TLAS後、Lighting前にcompute passを追加 |
| Engine resource lifecycle | scene render sizeの専用mask、SRV/UAV、resize/release対応 |
| `RootSignatureLayout.h` / root signature生成 | Lighting向けmask SRVを独立して追加。既存slotを流用しない |
| `shaders_LightPass.hlsl` | `iblDiffuse`だけへ可視率を適用 |
| `SceneRenderer` / settings / shared debug UI | backend-neutralな設定とeffective status、mask preview |
| CMake / vcxproj | compute shader生成とruntime配置検査への登録 |

`RayQueryShadowPass`のworld position復元とTLAS利用は参考にするが、出力と設定は共有しない。
既存Shadow shaderの単純な`Proceed()`ループをそのままmaterial対応と解釈しない。

RenderGraphのreadはDepth/NormalのNON_PIXEL_SHADER_RESOURCE、TLASの
RAYTRACING_ACCELERATION_STRUCTURE。専用maskへUAV書込み後、LightingがPS SRVとして読む。
maskは初版R16_FLOAT、0=遮蔽、1=可視。backgroundは1。無効時はpassを省略し、
Lightingの無効分岐でSRVをsampleしない。未初期化maskや前sceneの値を使わない。

mask寸法はwindow寸法でなくsceneの内部render sizeと一致させる。
full-resolutionを初版とし、half-resolution化やbilateral upsampleは画質確認後に分離する。

## 設定と実行条件

設定案は `DiffuseEnvironmentVisibilitySettings` とし、次の値を持つ。

- `enabled=false`
- `strength=1`、finiteな[0,1]
- `sampleCount=16`、初版では1〜64の明示値
- `normalBias`、`rayTMin`、`traceDistance`。有限・非負、`traceDistance > rayTMin`

初期biasは既存Shadow設定のscale問題を参考にするが、Shadow設定の変更と連動させない。
GBuffer shading normalでのoffsetは実geometryからずれる場合がある。平面・球・normal mapありの
self-hit検証を行い、必要なgeometric normal導入は別の明示変更とする。

初版はDeferred HybridかつRayQuery対応device、opaqueのみのsceneを対象とする。
MASK/BLEND、透過・Volumeなど遮蔽意味が未定義なsceneは試作を無効にして理由を表示する。
requested enabledは保持し、effective enabledと理由を別に表示する。
既存のPT/RR admission状態を書き換えない。

rayはsurfaceから離れる方向に出し、opaque triangleの両面を遮蔽物として扱う。
`ACCEPT_FIRST_HIT_AND_END_SEARCH`を使い、背景や材質判定を省略してよい条件を明記する。

`traceDistance`はworld unit。短い値は局所AOに近く、遠い屋根を検出できない。
検証では遮蔽物を含むscene boundsとray originの距離から必要な上限を求め、
rayがscene boundsを抜けるまで届くことを確認する。有限距離内のmissを無条件に
無限遠のsky visibilityと同一視しない。動的sceneではboundsも更新が必要。

## ノイズとコスト

初版のsample列はpixelごとに固定し、frameごとのjitterやtemporal historyは追加しない。
画面上の固定noiseや移動時の揺れは計測対象とする。直接光のsoft shadow用disk sampleを
hemisphere sampleとして流用しない。

16 rays/pixelなら640x360で最大3,686,400 rays/frame、1920x1080で33,177,600。
実時間運用に十分軽いとは仮定しない。専用GPU timestampでOFF/ONの差を測る。
初版は方式評価用であり、低SPP・時間蓄積・denoiseの採用は測定後に決める。

## 検証順序と判定

1. CPU/GPU sampling: 単位長、同一半球、cosine分布、全hit=0/全miss=1、
   NaN設定拒否。既存rayqueryを使うGPU fixtureでmaskの意味を確認。
2. Cornell: 同じカメラと受光面、開口あり/なし、一様環境、直接光/発光/Specular OFF。
   閉じた箱の内部maskは0、IBL OFF時と同じ黒。開口ありmaskは0より大きく1未満。
   CPUから同じ点で開口への可視率を積分し、sample budgetから事前に定める誤差内か確認。
3. 距離対照: 近い遮蔽物と遠い屋根を用意し、traceDistance不足と十分な場合を区別。
4. 配光対照: 一様環境に加え、明るい方向を塞ぐ/暗い方向を塞ぐケースを比較。
   scalar visibilityの限界を数値と画像で記録する。
5. Sponza: Diffuseのみ、Specularのみ、両方、直接光のみをOFF/ON比較。
   共通点でSpecular/直接光の成分が変更されないことを確認する。
6. 動的遮蔽物、camera移動、resize、scene切替、設定復元、unsupported fallback。
   古いmask、geometry未更新、表示位置ずれ、self-shadow flickerを確認。

PNGは目視用。maskとpre-tone-map linear HDRをreadbackして定量評価する。
既存PNGの平均値を線形の光量や誤差率として使わない。
GPU timingsとD3D12 Debug Layer、CMake/MSBuild/shader manifest、focused testsを記録する。

## その後の判断

scalar方式が一様環境で成立しても、方向付き照明の誤差が大きければ、
`E_visible = pi/N * sum(L_environment(w_i) * visibility(w_i))`
を直接推定する方式を次の試作とする。この場合、既存Diffuse irradianceの項を
置き換え、追加加算しない。明るい小面積光源はcosine samplingだけでは分散が高いため、
環境importance sampling/MISの検討が必要になる。

実装単位は (A) sampling/pass/mask診断、(B) Lighting合成と公開設定、
(C) Cornell/Sponza/GPU cost検証を推奨する。新規module中心のAを先行し、
BのEngine/descriptor/settings編集はwork-3/4の統合点を確認してからまとめる。
DDGI、Diffuse多重反射、色移りはこの試作の後に独立して検討する。
