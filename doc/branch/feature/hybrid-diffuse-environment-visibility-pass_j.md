# Diffuse Environment Visibility 診断pass

作成日: 2026-10-31
branch: `codex/hybrid-diffuse-ibl-diagnosis`
基点: PR #109 / `12912e2`

## 実装範囲

DeferredのRender Viewに`Diffuse Environment Visibility`を追加した。
standalone Debug UIとhost共通のSceneRendererDebugUiの双方で選択できる。
hostは既存APIでも選択できる。

```cpp
renderer.SetRenderingPath(RtPbrSurveyEngine::RenderingPath::Deferred);
renderer.SetRenderViewMode(RtPbrSurveyEngine::RenderViewMode::DiffuseEnvironmentVisibility);
```

専用compute passがGBuffer Depth/NormalとTLASを読み、R16_FLOAT maskへ書く。
専用debug passがmaskをscene colorへ表示する。Lightingへは合成していない。
白は可視率1、黒は0、マゼンタは非対応または無効な入力normal/positionを示す。
backgroundは1。通常のLightPassではcompute/debug passを実行しない。

初版は16本の固定cosine hemisphere sample、normal bias 0.001、TMin 0.0001、
trace distance 10000 world units。時間変化するsampleや蓄積・denoiseはない。
全rayはopaque triangleの両面で遮蔽される。初版にはsample数等の調整UIはない。
有限距離内の可視率であり、10000を超える遮蔽物を含むsceneは検証対象外。

MASK・Thin Transmission・Volumeを含むsceneは、既存scene classificationから
非対応として扱う。requested PT/RR設定は変更しない。初版では材質候補の透過判定を
実装していないため、これらを含む実assetの改善評価にはまだ使用しない。

render sizeに合わせてmaskを作成/再作成する。SRV/UAVはShadow Maskから独立し、
RenderGraphにDepth/Normal/TLAS read、mask UAV writeとdebug SRV readを宣言する。
専用compute root signatureを使うため、Lightingのroot signatureは変更しない。
scene modeの既存enum値を維持し、診断値を末尾の42へ追加した。

## 数値検証

`Tests/PathTracing/validate_diffuse_environment_visibility.py`は4条件をEXRとして取得し、
`RtPbrSurvey.DiffuseEnvironmentVisibilityTests`でTinyEXR decodeして検証する。
EXRはdebug passの出力をtone-map前に保存するため、可視率をlinear値として比較できる。
preview PNGは検証ツールがEXRを固定[0,1]で変換したもので、通常のtone-mapped画像とは異なる。

```powershell
python Tests/PathTracing/validate_diffuse_environment_visibility.py --output bin/PathTracingValidation/env-mask-new --validator build/cmake-host-debug/Debug/RtPbrSurvey.DiffuseEnvironmentVisibilityTests.exe
```

CPU referenceはCornellの同じback wall上の点から開口矩形との交差をdoubleで求める。
GPUのmask readbackや期待画像を参照計算へ流用しない。各ケースの中央ROIは24,000画素。
開口境界のfloat丸めを考慮し、事前にmean absolute error <= 0.002、
不一致画素率 <= 1%を上限に設定した。閉鎖/非遮蔽/非対応の各対照は完全一致を要求する。

最終結果: `bin/PathTracingValidation/hybrid-env-mask-r2-20261031/mask-report.json`。

| 条件 | 結果 | CPU referenceとの差 |
| --- | --- | --- |
| Cornell 開口あり | 平均可視率0.242271 | 全24,000画素で0 |
| Cornell 閉鎖 | 全0 | 全24,000画素で0 |
| 遮蔽物なし平面 | 全1 | 全24,000画素で0 |
| Thin材質 | 全マゼンタ | 全24,000画素で0 |

4プロセスとも正常終了し、D3D12 ERROR/CORRUPTIONは0。既存のresource初期state警告は残る。
最初の非対応fixtureはtransmission JSONのtint/ior不足でscene loadに失敗したため、
失敗記録を`hybrid-env-mask-20261031`へ保持し、修正fixtureで全4件を再実行した。

## Build と残課題

- 通常vcxproj Debug x64成功。既存MSB4011警告1件。
- SDK-free CMake SceneRenderer/standalone成功。
- focused CTest 6/6: 開口reference、runtime shader manifest、render graph document、
  renderer settings、camera projection、host debug UI API。
- 通常LightPassのCornell diffuse-onlyを再撮影し、導入前PNGのSHA256と完全一致。
  `hybrid-ibl-cornell-r2-20261031/cornell/open-diffuse/post-mask-regression.json`に
  導入前/後hashを保持。同folderのcapture/debug logはこの再実行で更新されているため、
  古いreportのlog hashではなくpost-mask-regressionのhashを参照する。
- CMake ALL_BUILDの初回は既存Mp4EncoderWorkerTestsのshader配置copyで失敗した。
  同targetを単独で再buildして成功。共通出力先への並行copy競合の疑いがあり、
  全targetの初回成功とは報告しない。

未検証: UIでの実クリック、Sponza等の非opaque scene、動的遮蔽物、resize/scene切替の
連続試験、専用GPU時間の測定、低SPP/時間蓄積、lighting合成。
次はこのmaskをDiffuse IBLだけへ合成する別increment。
