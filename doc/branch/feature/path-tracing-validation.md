# Path Tracing Validation

## 1. Purpose

この手順はPath Tracingの固定sample出力、GPU timing、D3D12 Debug Layer、Evaluation CaseとROIを同じ条件で
記録する。Native render-sizeのreference captureを対象とし、DLSS SR/RRは実行しない。

## 2. Fixed-sample contract

- `-PathTracingSamples <N>`はaccumulated sample countがNになった時点でaccumulationを停止する。
- 自動captureでは`Samples / Frame`を1へ固定し、Nを超えるframeを描かない。
- `-PathTracingSeed <N>`はpath RNG seedを固定する。
- `-EvaluationCase <name>`は保存済みscene、camera、rendering settings、ROI、日本語comment、test itemを復元する。
- `-EnablePathTracing`はDLSSと排他的である。Path Tracing CLIでは未使用のStreamline SDKを初期化しない。

## 3. Reference command

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -SceneName DamagedHelmet `
  -Samples 64 `
  -Seed 1
```

Evaluation Caseを使う場合:

```powershell
.\Tests\PathTracing\Invoke-ReferenceCapture.ps1 `
  -EvaluationCaseName "PT Normal Map" `
  -Samples 64 `
  -Seed 1
```

既定出力は`bin/x64/Debug/PathTracingReference`であり、PNG、log、JSON、Markdownはcommitしない。

Guide bufferを確認する場合は`-DebugPreviewResource`に次のresource名を指定する。

| Resource | Format | Contract |
|---|---|---|
| `PathTracing.NormalRoughness` | `R16G16B16A16_FLOAT` | world normal XYZ、roughness A |
| `PathTracing.ViewZ` | `R32_FLOAT` | positive linear view-Z、miss 0 |
| `PathTracing.MotionVectors` | `R16G16_FLOAT` | previous NDC - current NDC |
| `PathTracing.Albedo` | `R16G16B16A16_FLOAT` | linear albedo RGB、hit mask A |
| `PathTracing.DiffuseRadianceHitT` | `R16G16B16A16_FLOAT` | current-frame diffuse-attributed radiance RGB、raw primary hit distance A |
| `PathTracing.SpecularRadianceHitT` | `R16G16B16A16_FLOAT` | current-frame specular-attributed radiance RGB、raw primary hit distance A |

Environment比較では、Primary missの可視背景とSecondary missのIBLを別々に確認する。
`Show Skybox`を無効にした場合、Primary missはclear colorへ切り替わるが、Path Tracingの`Environment`が有効なら
Secondary missのIBL寄与は残る。`Show Skybox`を有効にした場合、Primary missはHybrid rendererと同じenvironment cubeを
`IBL Intensity`で再スケールせず表示する。両rendering pathは同じToneMap設定を通す。

```powershell
.\bin\x64\Debug\RtPbrSurvey.exe `
  -AutoSelectGltfAsset DamagedHelmet `
  -UseSceneDefaults `
  -EnablePathTracing `
  -DebugPreviewResource PathTracing.NormalRoughness
```

複数sample/frameでもprimary-surface guideはframe内の最初のprimary sampleを表す。Diffuse/Specular signalはframe内sampleを
平均する。いずれも現在frameの値でありprogressive accumulationしない。RadianceHitTのAはbackend-neutralなraw hit distanceで、
NRD/DLSS RR向けpacking済みデータではない。

## 4. Diagnostics contract

| Field | Meaning |
|---|---|
| `gpuTimeMs` | GPU timestampによる`PathTracingPass`単体の時間 |
| `gpuTimingSampleCount` | 固定sample capture中に取得できたGPU timestampの件数 |
| `gpuTimeAverageMs` | 固定sample capture中の`PathTracingPass`平均GPU時間 |
| `gpuTimeMinMs` / `gpuTimeMaxMs` | 固定sample capture中の`PathTracingPass`最小/最大GPU時間 |
| `primarySamplesPerFrame` | `renderWidth * renderHeight * samplesPerFrame` |
| `maxPathSegmentsPerFrame` | primary samplesとmax bounceから求めるpath segment上限 |
| `maxRayQueriesPerFrame` | path segmentとdirect-light shadow queryを含むRayQuery上限 |
| `primarySamplesPerSecond` | primary samplesをPathTracing pass GPU時間で除した参考throughput |

`maxPathSegmentsPerFrame`と`maxRayQueriesPerFrame`はmiss、Russian Roulette、無効light、遮蔽による早期終了を
数えない上限値であり、実発行ray数ではない。

## 5. Pass criteria

- A/B PNGのSHA-256が一致する。
- 両captureのaccumulated samples、seed、render size、settingsが一致する。
- D3D12 `ERROR`と`CORRUPTION`が0件である。
- Evaluation Case指定時はROI、comment、boolまたは1-5のtest itemがreportへ残る。
- metallic/roughness response、normal map、shadow、emissive/environmentを目視確認する。
- 4 primary-surface guideと2 noisy radiance signalを個別にDebug Texture Previewで開ける。
- 静止sceneのMotionVectorsは0付近、camera/instance移動では既存GBufferと同じ向きに変化する。
- surface hit領域でDiffuse/Specular signalが同一画像ではなく、primary miss領域では両方0になる。

buffer initial stateがCOMMONへ扱われる既知のwarning 2件/captureはerror判定に含めない。

## 6. Hardware matrix

| GPU | Driver | Commit | Scene / samples / seed | GPU ms | Hash | D3D12 errors | Status |
|---|---|---|---|---:|---|---:|---|
| RTX 2080 Ti | WMI unavailable | Commit 7 working tree | DamagedHelmet / 64 / 1 | A: 2.814 avg, B: 3.588 avg | `24638B264811C1E235C9D6AB8932EEC1EB17E0D370244C3026E0254195CE0840` | 0 | Pass |
| RTX 4090 | pending report | pending | DamagedHelmet / 64 / 1 | pending | pending | pending | Not recorded |

4090の結果は別PCで同じscriptを実行し、生成された`path-tracing-reference.json`の値をこの表へ転記する。
2080 Tiのdriver versionは実行環境のWMI queryが拒否されたため未記録。adapter名、vendor/device ID、VRAMは
アプリが選択したDXGI adapterから記録している。A/Bとも64 samples、同一PNG hash、既知warning 2件、error 0件。
Commit 8のguide buffer追加後も同じ64-sample hashを維持し、4 resource個別のDebug Preview実行はerror 0件。
