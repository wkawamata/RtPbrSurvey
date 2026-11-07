# Diffuse IBLへの環境可視率合成

作成日: 2026-11-01
branch: `codex/hybrid-diffuse-ibl-diagnosis`、基点 `12912e2`。

## 挙動

[診断pass](hybrid-diffuse-environment-visibility-pass_j.md)のmaskを、Deferred LightPassの
Diffuse IBLだけへ次の式で適用する。既定はOFF。

```text
diffuseIbl *= lerp(1, visibility, strength)
```

直接光、Specular IBL、emissive、Hybrid Reflectionの加算項をこの係数で乗算しない。
material AOは従来のまま別途適用される。PrimaryのDiffuse IBLだけを対象にするため、
鏡面反射先のDiffuse IBL遮蔽や壁の色移り・多重反射は追加していない。

有効条件はDeferred + RayQuery対応 + TLASあり + opaque scene + requested enabled +
strength > 0 + Diffuse IBL ON + IBL intensity > 0。
MASK/Thin/Volume sceneはrequested値を保持したまま実効OFFとなる。
Forward/PT/RR/NRDの照明計算には適用しない。
診断Render Viewはenabledとは独立してmaskを計算する。

## 公開設定と保存

```cpp
RtPbrSurvey::DiffuseEnvironmentVisibilitySettings settings;
settings.enabled = true;
settings.strength = 1.0f;
settings.sampleCount = 16;
settings.normalBias = 0.001f;
settings.rayTMin = 0.0001f;
settings.traceDistance = 10000.0f;
renderer.SetDiffuseEnvironmentVisibilitySettings(settings);
bool active = renderer.IsDiffuseEnvironmentVisibilityActive();
const char* reason = renderer.GetDiffuseEnvironmentVisibilityStatus();
```

設定型はD3D12型を含まない`Shared/DiffuseEnvironmentVisibility.h`。
strengthは[0,1]、sampleCountは[1,64]。bias/TMinは有限の非負値、
traceDistanceは有限かつTMinより大きい値。無効な入力のsetterは
`std::invalid_argument`を投げ、現在の設定を置換しない。

standalone/host共通の`SceneRendererDebugUi::DrawDiffuseEnvironmentVisibility()`を用意し、
両Debug UIの`Diffuse IBL Visibility`セクションから操作できる。
Enabled、Strength、Rays Per Pixel、Normal Bias、Ray TMin、Trace Distanceと
実効ON/OFF・対応条件の状態を表示する。

`CaptureSettings`/`ApplySettings`、renderer JSON、standalone SceneConfigの保存/復元に対応。
render-preset.jsonの例:

```json
{
  "schemaVersion": 4,
  "diffuseEnvironmentVisibility": {
    "enabled": true,
    "strength": 1.0,
    "sampleCount": 16,
    "normalBias": 0.001,
    "rayTMin": 0.0001,
    "traceDistance": 10000.0
  }
}
```

既存schemaの追加optional fieldとして扱い、fieldがない古い設定は既定OFFを維持する。
deserializeの無効値は失敗とし、呼び出し側の既存設定を変更しない。

## Descriptorとroot signature

共通graphics root signatureは既に64 DWORDなので、新しいroot parameterは追加しない。
既存ToneMapSceneColor tableにspace18のSRV rangeをoffset 2として追加する。
Lightingがbindするstaged tableはShadow SRV/UAV/可視率SRVの3 slotsで、
mask本体・compute用SRV/UAVは独立したままalias descriptorを追加する。
他のshaderは当該tableの最初のrangeだけを使用する。

Lighting向けstrengthは既存b3 root constantsの先頭1 DWORDをpass固有に設定する。
ToneMap等は各pass自身のconstants handlerを維持する。
root signatureの64 DWORD上限はcompile-time assertionで検査する。

最初のGPU実行はstaged allocatorの初期容量4に対し5 slotsを要求し失敗した。
既存grow policyが64 slots単位で予約64を超えるため、今回必要な5 slotsを初期確保する
よう修正した。失敗runと一時診断ログはbin配下に保持し、診断用catch/logコードは除去した。

## 検証

`Tests/PathTracing/validate_diffuse_environment_composite.py`が10条件を起動し、
pre-tone-map EXRをTinyEXRでdecodeして比較する。出力は独立したfresh directoryを使う。

```powershell
python Tests/PathTracing/validate_diffuse_environment_composite.py --output bin/PathTracingValidation/env-composite-new --validator build/cmake-host-debug/Debug/RtPbrSurvey.DiffuseEnvironmentVisibilityTests.exe
```

合成式のreferenceは、OFF画像と独立CPUのCornell開口可視率から生成する。
half-float保存の丸めを考慮し、RGBの最大絶対誤差0.002を事前上限とする。
強度0とSpecular/直接光の不変性は、全画面RGBAの完全一致と非blackな正の対照を要求する。
PNG previewはEXRを固定[0,1]表示したものであり、HDRの測定値はEXRを使う。

最終の有効な撮影結果は次の2つのreportに分かれる。

- `bin/PathTracingValidation/hybrid-env-composite-r2-20261101/report.json`の先頭8件
- `bin/PathTracingValidation/hybrid-env-composite-direct-20261101/report.json`の直接光2件

最初のrunはdescriptor容量不足で起動失敗した。容量修正後のrunではDiffuse/Specularの
8件が成功したが、直接光fixtureの必須JSON項目不足で最後の対照が失敗した。
light position/range/coneを補ったfixtureで直接光2件だけを再実行した。
失敗runを削除したり、最初のreport全体を成功と書き換えたりしていない。

| 条件 | 中央ROIのlinear RGB平均 | referenceとの最大RGB絶対差 |
| --- | ---: | ---: |
| 開口あり、強度0 (OFFと同値) | 0.364502 | 0 |
| 開口あり、強度0.5 | 0.226396 | 0.000106812 |
| 開口あり、強度1 | 0.0883108 | 0.0000305176 |
| 閉じた箱、強度1 | 0 | 0 |
| Specularのみ、ON/OFF | 0.0114121 | 0 |
| 直接光のみ、ON/OFF | 0.230543 | 0 |

強度0/Specular/直接光は、中央ROIに限らず全画面RGBAで完全一致し、
reference画像のRGBが非zeroであることも要求した。
成功した10プロセスにD3D12 ERROR/CORRUPTIONはない。既存のresource初期state警告は残る。

通常vcxproj Debug x64成功（既存MSB4011警告1件）、SDK-free CMake SceneRenderer/standalone成功。
公開UI/設定/render graphのtest targetを最終ソースで再buildし、focused CTest 6/6成功。
設定testは全fieldのround-trip、古い設定の既定OFF、NaN/Inf/range外の拒否、
無効JSONの非破壊性を確認する。

新fieldを含まない古いpresetでも通常PNGを再撮影し、導入前の画像SHA256と完全一致した。
記録: `bin/PathTracingValidation/hybrid-ibl-cornell-r2-20261031/cornell/open-diffuse-composite-off-20261101/regression.json`。
合成比較preview: `hybrid-env-composite-r2-20261101/composite-contact.png`。

GUIの実クリックと連続lifecycleは未確認。生成されたscene/presetはScene Editorでも開ける。

## 通常表示での比較

raw linear previewだけでは形状と遮蔽の差を読み取りにくいため、Cornellを外から見る
共通カメラで通常PNGを撮影した。箱全体・床・左右の壁・鏡面球・拡散球を画面内に含める。

```powershell
python Tests/PathTracing/capture_cornell_ibl_comparison.py --output bin/PathTracingValidation/cornell-ibl-visual-new --samples 512
```

撮影結果: `bin/PathTracingValidation/cornell-ibl-visual-square-20261101/`。
`report.json`に入力・実行ファイル・画像・ログのhashと実行commandを記録した。

- `environment-comparison.png`: 一様環境光のみ。
- `environment-direct-comparison.png`: 同じ環境光と共通の平行光源。
- 左から従来Hybrid、Diffuse IBL遮蔽ON、Native PT。
- 全画像720x720、camera position `(0, 0.3, -6)`、target `(0, -0.15, 0.3)`、FOV Y 42度。
- 露出1、ACES、RR/NRD OFF。遮蔽64 rays/pixel、PT 512 spp / 8 segments。
- Hybrid Reflectionは両Hybrid条件で有効のまま固定した。

6 capturesは完了し、D3D12 ERROR/CORRUPTIONは検出されなかった。
環境光のみでは、遮蔽ONで拡散球と床の接地感が戻る。一方、固定samplingの段階状模様は
64本でも目立つ。PTにある間接反射・色移りはscalar visibilityでは再現できず、
Hybrid Reflectionのhit surfaceにおけるDiffuse IBLは今回の遮蔽対象外なので、
鏡面球の反射が明るい問題も残る。この比較はPTの収束や画素一致を証明するものではない。

## 制約

模様の抑制可否は[分散samplingとfilterの実現性検証](hybrid-diffuse-environment-visibility-pattern-study_j.md)
で別途確認し、その後[本体へ組み込んだ](hybrid-diffuse-environment-visibility-integration_j.md)。

一様環境向けのscalar visibility近似であり、太陽など方向性の強いHDRIの遮蔽積分とは異なる。
16本の固定samplingによる段階状の境界があり、時間蓄積/denoiseはない。
遠方遮蔽物はtraceDistance以内に入る必要がある。biasはscene scaleに合わせる必要がある。
MASKを含むSponza等の実assetは、material-aware candidate処理の後続対応が必要。
GPU時間、動的遮蔽物、連続resize、UI実クリックの評価はこのincrementでは未完了。
