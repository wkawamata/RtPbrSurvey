# Hybrid Reflection Production Quality Gate

## 目的

本文書は、Hybrid Reflection機能をdefault-offの診断実験からproduction候補へ変更するために必要な根拠を定義する。本文書だけで現在の実験機能を昇格させるものではない。

現在の既定経路を保護baselineとする。Hybrid Reflection自体はONだが、stochastic rough samplingはOFF、temporal history weightは`0.0`であり、rejected-pixel、edge-aware spatial、spatiotemporal spatial policy、variance-guided temporalの各実験はOFFである。

## 判定区分

| 区分 | 意味 | default規則 |
|---|---|---|
| Production baseline | regressionを許容しない既存挙動 | default-onを維持可能 |
| Production candidate | 宣言したscene／hardware範囲の必須gateをすべて通過 | default昇格には別の明示判断が必要 |
| Diagnostic experiment | 測定、比較、限定調査に有用 | default-offを維持 |
| Rejected experiment | 意図した品質主張に失敗、または説明できないregressionを導入 | default-offを維持し、診断価値がある場合だけ残す |

限定testの合格はproduction昇格と同義ではない。`PASS WITH LIMITATION`は有用な限定根拠だが、それだけではproduction gateを満たさない。`NOT CLAIMED`は根拠から主張できない結論を示す。

## 評価profile

| Profile | Stochastic sampling | Temporal weight | Edge-aware spatial | Bounded spatial policy | 目的 |
|---|---:|---:|---:|---:|---|
| Baseline | OFF | `0.0` | OFF | OFF | 保護対象の既定値とregression基準 |
| Temporal candidate | ON | `0.9` | OFF | OFF | temporal noise低減と追従性の検証 |
| Fixed spatial diagnostic | ON | `0.9` | ON | OFF | 既存固定filterとの比較 |
| Bounded spatial diagnostic | ON | `0.9` | ON | ON | confidence／varianceで制限した比較 |

Variance-guided temporalは独立したdefault-off診断であり、spatial policy比較へ暗黙に組み合わせない。将来combined profileを作る場合は、独立したpaired根拠を必要とする。

## 必須scene coverage

| Scene | 必要な根拠 |
|---|---|
| `Hybrid Reflection Estimator Test` | roughness `0.0`から`1.0`、metallic／dielectric差、mirror bypass、mean維持、variance、frame difference |
| `Hybrid Reflection Spatial Filter Test` | 直線／曲線のmaterial・geometry境界、emissive reflection、色漏れ、blur、bounded policy挙動 |
| DamagedHelmet | texture／normal mapを含むproduction asset、既知の後頭部寄り面／下面pipe、motion、反転、停止後収束、Lit寄与 |
| 追加glTF assetを最低1つ | DamagedHelmetまたはprocedural controlled scene固有の判断ではないこと |

追加glTF assetは、単にloadできるsceneではなく、Hybrid Reflectionを視認できるsceneを選ぶ。適切なreflectionを視認できない場合は`PASS`ではなく`UNABLE`とする。

## Acceptance gate

| Gate | PASS | PASS WITH LIMITATION／failure条件 |
|---|---|---|
| Default維持 | 実験無効時にpassをbypassするかbaseline出力を維持し、default値が変化しない | 説明できないbaseline差はfail |
| Linear-HDR mean | 必須ROIすべてでpaired長期mean差が事前宣言threshold内 | 一部ROIの合格で別領域のmean shiftを隠さない |
| Temporal安定性 | varianceとframe-difference tailが改善、または宣言したnon-regression範囲内 | variance低減とtail悪化を平均して隠さない |
| Spatial integrity | depth、normal、roughness、hit class、hit distance、hit normal境界を越える色漏れが目視／測定でない | varianceが改善してもblur／色漏れはfail |
| Dynamic response | camera移動、反転、停止後収束で許容できない遅延、残像、古いreflectionがない | debug単体の遅延とLitへの影響を分けて記録 |
| Lit品質 | 輝度、reflection identity、細部、emissive寄与を維持 | 「差が見えない」はnon-regressionのみで、改善の根拠ではない |
| Runtime安全性 | Debug x64と影響HLSL build、新規D3D12 error 0件、新規warning 0件 | 既知warningは列挙する |
| Performance | 宣言hardware／resolutionでGPU costとmemory差を測定 | 未測定ならproduction昇格不可 |
| 再現性 | paired runのsample index、reset、camera、animation、warm-up、frame範囲が一致 | sequence不一致ならA/B帰属は無効 |

数値thresholdは結果を見てから推測せず、各reportで事前に記録する。256 framesを標準定量gate、64 framesを開発check、1024 framesを必要時のdrift／firefly追加監査とする。

version管理されたROI sourceは`Tests/SubjectiveValidation/HybridReflection/production-quality-gate-profiles.json`とする。production gate runは`Invoke-ProductionQualityGates.ps1`からprofile名で選択し、手作業で転記した座標は開発probeとして扱い、gate根拠には採用しない。runnerはpaired sequence一致、resolved-radiance control variance不変、宣言mean boundをinvariantとして判定する。varianceとframe-difference変化は自動的な品質主張に変換せず、観測値としてreportする。

## 現在の分類

| 機能 | 現在の区分 | 根拠 |
|---|---|---|
| Deterministic Hybrid Reflection baseline | Production baseline | 現在の保護対象default |
| Stochastic rough sampling＋temporal accumulation | Diagnostic experiment | noise低減は観測済みだが、estimator correctness、performance、広いproduction scene coverageが未完了 |
| Fixed edge-aware spatial filter | Diagnostic experiment | 限定variance／noise形状への効果はあるが、主観効果が弱くproduction generalization未確立 |
| Bounded spatiotemporal spatial policy | Diagnostic experiment | paired meanを維持し一部frame-difference tailを低減するが、temporal variance増加例があり、限定Lit評価ではA/B差を知覚できなかった |
| Variance-guided temporal policy | Diagnostic experiment | controlled／DamagedHelmetの限定根拠はあるがproduction defaultではなく、現在のspatial policy profileにも含めない |

## 主張境界

このPhaseで主張可能なのは、現在のdefaultが安全に維持されること、診断が再現可能であること、宣言した品質gateに対して各実験profileを合否分類できることまでである。物理的正しさ、unbiased estimator、Path Tracing一致、DLSS Ray Reconstruction readiness、全sceneへのgeneralizationは主張しない。

名前付き全profileの64-frame開発runは、scheduling、resolved control、meanの全invariantに合格した。ただし一貫した品質改善は確立しなかった。256-frame標準runも同じ結論を再現した。DamagedHelmet後頭部寄り面profileは無変化を維持し、下面pipe profileはmean差を`0.5%`以内（`0.426%`）に維持したが、frame-difference p99が`15.99%`低下する一方でtemporal varianceは`21.44%`増加した。controlled roughness `0.35`／`1.0`でもframe-difference tail低減とtemporal variance増加が併存した。このmixed resultによりbounded policyはdiagnostic区分を維持する。

