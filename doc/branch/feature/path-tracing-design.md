# Path Tracing Implementation Design

## 1. Goal

RtPbrSurvey に、既存の Forward / Deferred と独立した Path Tracing rendering path を追加する。

最初の到達点は、既存 scene、camera、TLAS、material、texture を使い、Inline RayQuery compute shader で
progressive path tracing を実行して tone mapping 前の HDR scene color を生成することである。

この設計では次を重視する。

- Raster / Hybrid と同じ scene を切り替えて比較できる。
- RenderGraph 上で Path Tracing pass と resource の依存関係を確認できる。
- Evaluation Case で camera、ROI、rendering settings、判定項目を固定できる。
- accumulation の破棄条件を明示し、古い履歴を誤って再利用しない。
- 既存の Hybrid Reflection 用 RayQuery 資産を再利用し、初期実装で full DXR pipeline を導入しない。
- DLSS SR / RR は Path Tracing の基準画像が成立するまで分離する。

## 2. Non-goals for the first implementation

初期実装では次を対象外とする。

- DXR ray generation / miss / closest-hit shader と shader table を使う full DXR pipeline
- ReSTIR、adaptive sampling、wavefront path tracing
- transmission、volume、participating media
- alpha blend / alpha mask geometry
- path-traced depth を利用した raster debug-line 合成
- DLSS Ray Reconstruction を一般的な path tracing denoiser として利用すること
- path tracing 専用の environment-map importance sampling table
- multi-GPU、asynchronous compute

## 3. Existing contracts to reuse

### 3.1 Ray tracing support

`Renderer/RayTracingSupport` は `D3D12_FEATURE_D3D12_OPTIONS5` を問い合わせ、ray tracing tier を保持する。
Path Tracing の Inline RayQuery shader は `cs_6_5` と DXR 1.1 を要求するため、Path Tracing の利用条件は
`D3D12_RAYTRACING_TIER_1_1` 以上とする。

単に `RayTracingSupportInfo::IsSupported()` を使うのではなく、Path Tracing の support 表示では tier 1.1
要件を明示する。

### 3.2 Scene geometry and acceleration structures

既存 renderer は次の GPU data を保持している。

- packed `SceneVertex` buffer
- optional 32-bit index buffer
- per-frame `InstanceData` buffer
- `SceneMesh::Range` buffer
- material structured buffer
- bindless scene texture descriptor table
- one BLAS per mesh range and a TLAS containing scene instances

`shaders_HybridReflection.hlsl` は committed hit の primitive index、barycentrics、instance ID から triangle、
UV、normal、material を復元している。Path Tracing ではこの処理を複製せず、再利用可能な HLSL include
へ段階的に抽出する。

候補 include:

- `Shaders/SceneRayQuery.hlsli`: vertex/index/instance/mesh-range access と hit reconstruction
- `Shaders/PathTracingSampling.hlsli`: RNG、disk/hemisphere/GGX sampling
- 既存 `Shaders/Material.hlsli`: material layout

`GltfVertex` には position、UV、normal、tangent、material ID がある。初期版は interpolated vertex normal
を使用し、normal map は tangent-space reconstruction の検証後に追加する。

### 3.3 Render and output dimensions

- Path Tracing dispatch と accumulation resource は render size を使う。
- swap chain、back buffer、ImGui、ToneMap destination は output size を使う。
- Path Tracing 自身が pixel sample jitter を生成する。DLSS 用 temporal camera jitter は Path Tracing 中に
  projection matrix へ重ねない。
- render size が変わった場合は accumulation を必ず reset する。

### 3.4 Scene color handoff

Path Tracing は `LightPass.RenderTarget` を偽装して書き換えない。専用 resource
`PathTracing.SceneColor` を生成し、ToneMap の source selection で選択する。

初期の frame flow:

```text
Clear
  -> optional PathTracingHistoryClear
  -> PathTracingPass
      -> PathTracing.Accumulation
      -> PathTracing.SceneColor
  -> ToneMapPass
  -> DebugTexturePreviewPasses
  -> ImGuiPass
```

Path Tracing では DepthPrePass、GBuffer、RayQueryShadow、HybridReflection、LightPass、DebugLine、
TemporalUpscaler を実行しない。これらを必要とする追加機能は個別に設計する。

## 4. Renderer model

### 4.1 Rendering path

`RtPbrSurveyEngine::RenderingPath` に `PathTracing` を追加する。

```cpp
enum class RenderingPath
{
    Forward = 0,
    Deferred,
    PathTracing,
};
```

既存の数値を維持するため末尾へ追加する。保存済み `SceneRendererSettings` と Evaluation Case の既存値を
壊さない。

### 4.2 Settings

初期設定型の候補:

```cpp
struct PathTracingSettings
{
    bool accumulate = true;
    UINT samplesPerFrame = 1;
    UINT maxBounces = 2;
    UINT randomSeed = 1;
    bool directLightingEnabled = true;
    bool environmentEnabled = true;
    bool emissiveEnabled = true;
    bool russianRouletteEnabled = false;
};
```

制約:

- `samplesPerFrame`: 1..16
- `maxBounces`: 1..16
- `randomSeed`: deterministic test 用に保存する。
- exposure と tone-map operator は post process なので Path Tracing settings へ重複させない。
- `accumulate=false` は毎 frame history を reset して単一 frame の結果を表示する診断モードとする。

`SceneRendererSettings` に `pathTracing` を追加し、JSON schema version を更新する。旧 schema の読み込みは
defaults を使って継続できること。Evaluation Case は `SceneRendererSettings` を内包しているため、追加後は
Path Tracing settings も自動的に snapshot 対象となる。

### 4.3 Runtime state

settings と runtime state を分ける。

```cpp
struct PathTracingRuntimeState
{
    uint64_t accumulatedSampleCount = 0;
    uint32_t frameSampleIndex = 0;
    bool historyValid = false;
    PathTracingResetReason lastResetReason = PathTracingResetReason::Initial;
};
```

sample count は全 pixel が同じ回数 sampling される初期版では CPU の global counter でよい。adaptive
sampling を導入するまでは per-pixel sample-count texture を追加しない。

## 5. GPU resources

### 5.1 Resource specifications

| Resource | Size | Format | Lifetime | Usage |
|---|---|---|---|---|
| `PathTracing.Accumulation` | RenderSize | `R32G32B32A32_FLOAT` | persistent | UAV + SRV、radiance sum |
| `PathTracing.SceneColor` | RenderSize | `R16G16B16A16_FLOAT` | persistent | UAV + SRV、normalized HDR output |

`PathTracing.Accumulation` は長時間 accumulation の overflow / precision loss を避けるため 32-bit float
とする。`PathTracing.SceneColor` は既存 HDR scene-color contract と合わせて 16-bit float とする。

初期版では shader が accumulation を read/write し、同じ dispatch で normalized average を
`PathTracing.SceneColor` に書く。別の sample radiance texture と resolve pass は追加しない。

### 5.2 Reset operation

history reset frame では accumulation を 0 に clear してから dispatch する。実装候補は次の順で選ぶ。

1. `PathTracingHistoryClearPass` として RenderGraph に conditional pass を追加する。
2. clear が既存 authoring API に適合しない場合のみ `PathTracingPass` の operation 冒頭で clear する。

RenderGraph と GPU capture で reset を識別できるため、第一候補を採用する。

前 frame の UAV write と次 frame の UAV read/write の間には必要な transition / UAV ordering を置く。
resource state は RenderGraph authoring の reads/writes に記述し、operation 内の隠れた barrier を増やさない。

### 5.3 Debug visibility

両 texture を RenderGraph resource node と Debug Texture source に登録する。

- `PathTracing.SceneColor`: Color semantic
- `PathTracing.Accumulation`: Color semantic、HDR exposure control 対応

PathTracing pass 選択時の related-resource inspector から個別 Preview と Preview All を利用できるようにする。

## 6. PathTracingPass

新規 renderer boundary:

- `Renderer/PathTracingPass.h`
- `Renderer/PathTracingPass.cpp`
- `Shaders/shaders_PathTracing.hlsl`

`PathTracingPassDesc` は SDK 型や engine UI 型を含めず、command recording に必要な D3D12 handle、GPU
address、dimensions、shader constants のみを受け取る。

root signature は少なくとも次を持つ。

- accumulation UAV
- scene-color UAV
- TLAS SRV
- camera CBV
- scene vertex root SRV
- scene index root SRV
- per-frame instance root SRV
- material buffer SRV
- bindless texture table SRV
- mesh-range root SRV
- path-tracing constants
- static sampler

Hybrid Reflection root signature と data layout は参考にするが、root parameter index を直接共有しない。
共通化は HLSL data contract と小さな descriptor-binding helper に限定し、巨大な汎用 root signature を
作らない。

## 7. Shader stages

### 7.1 Stage A: primary-hit baseline

- camera inverse view-projection から primary ray を生成する。
- pixel index、sample index、random seed から deterministic RNG state を作る。
- subpixel position を pixel 内で jitter する。
- opaque triangle に対して closest-hit RayQuery を実行する。
- miss は environment color、hit は normal / albedo / emissive の診断出力を選べるようにする。
- 1 spp/frame の progressive accumulation を成立させる。

完了条件は camera movement 中に履歴が混ざらず、camera 停止後に sample count が単調増加すること。

### 7.2 Stage B: direct lighting

- 既存 directional light の next-event estimation を追加する。
- shadow RayQuery で visibility を評価する。
- Lambert diffuse BRDF と emissive を加える。
- direct-light toggle が既存 LightingParams と整合する。

### 7.3 Stage C: indirect diffuse

- cosine-weighted hemisphere sampling を追加する。
- throughput と radiance を bounce loop で更新する。
- max-bounces setting を有効化する。
- surface self-intersection bias を world scale と設定値から適用する。

### 7.4 Stage D: metallic-roughness specular

- glTF metallic-roughness material を使う。
- GGX VNDF または同等の importance sampling を導入する。
- diffuse/specular lobe selection の PDF を throughput に反映する。
- normal map を tangent frame から適用する。

### 7.5 Stage E: convergence and performance

- Russian roulette
- direct-light / BSDF sampling の MIS
- environment importance sampling
- firefly clamp は既定 OFF の診断 option として追加
- GPU timestamp と rays/sample diagnostics

## 8. Accumulation reset contract

raw matrix や struct 全体の `memcmp` ではなく、変更箇所で revision を更新して history key を作る。

候補:

```cpp
struct PathTracingHistoryKey
{
    uint64_t cameraRevision;
    uint64_t sceneRevision;
    uint64_t materialRevision;
    uint64_t lightingRevision;
    uint64_t renderSizeRevision;
    uint64_t settingsRevision;
};
```

次の変更は reset を要求する。

- camera position、orientation、projection、FOV、near/far
- scene selection、mesh、instance count、instance transform
- material factor、texture、UV transform
- direct light、IBL、skybox、emissive contribution
- render width / height、render scale、window resize
- samples-per-frame 以外の radiance estimator 設定
- random seed、max bounces、sampling algorithm
- Path Tracing への切り替え
- Evaluation Case restore
- shader reload または path-tracing PSO recreation

次は reset しない。

- tone-map operator、exposure、paper white、display peak
- ImGui window、RenderGraph layout、Debug Texture window layout
- ROI と評価コメント
- pause/resume（停止中に他の reset 条件が変わった場合を除く）

`samplesPerFrame` の変更は estimator 自体を変えないため history を保持できる。ただし deterministic
capture の frame/sample mapping が変わることを report metadata に残す。

reset reason は bit flags または enum として記録し、UI と log に最後の理由を表示する。

## 9. UI and evaluation workflow

### 9.1 Rendering controls

Rendering Path selector に `Path Tracing` を追加する。Path Tracing panel の初期 control:

- Accumulate
- Samples / Frame
- Max Bounces
- Random Seed
- Direct Lighting
- Environment
- Emissive
- Reset Accumulation command
- Pause Accumulation

read-only diagnostics:

- Support: Available / Requires DXR 1.1 / Initialization Failed
- Render resolution
- Accumulated samples
- Current sample index
- Last reset reason
- GPU time（timestamp 対応後）

Path Tracing 選択中は DLSS SR と RR が実行されないことを status text で示す。既存 setting 自体は
破棄せず、Deferred へ戻したとき復元する。

### 9.2 Evaluation Cases

Path Tracing settings は SceneRendererSettings に保存される。固定 seed、固定 camera、固定 render size、
固定 accumulated sample count で比較を行う。

推奨 test item:

- `bool`: primary visibility is correct
- `bool`: accumulation resets after camera movement
- `bool`: no D3D12 debug-layer errors
- `int 1-5`: noise / convergence
- `int 1-5`: metallic highlight correctness
- `int 1-5`: diffuse indirect-light stability
- Japanese comment: reference difference と目視判断

CLI capture は最終 frame 数ではなく accumulated sample count を明記する。将来
`-PathTracingSamples <N>` と固定 seed option を追加し、N samples 到達後に capture する。

## 10. DLSS integration policy

### 10.1 Initial behavior

- Path Tracing では DLSS SR を実行しない。
- Path Tracing では DLSS RR を実行しない。
- Path Tracing は独自 subpixel sampling を使い、DLSS jitter state を利用しない。

この制約により、まず native render-size の unbiased/reference-oriented output を検証できる。

### 10.2 Later SR investigation

DLSS SR を接続する場合は、PathTracing.SceneColor を SR input として扱う前に次を検証する。

- motion vector、depth、exposure、jitter の contract
- progressive accumulation と temporal upscaler history の二重履歴
- render-size / quality-mode 変更時の両 history reset
- A/B 比較で native accumulation より bias や instability が増えないこと

### 10.3 Later denoising / RR investigation

DLSS RR は specular/diffuse input contract を持つため、単一 radiance output に後付けする一般 denoiser
として扱わない。必要な separated signals、hit distance、normal/roughness、motion/depth が Path Tracing
から正しく出せる段階で別 branch として再評価する。

## 11. Implementation steps and commit plan

### Commit 1: path-tracing mode and settings shell

- `RenderingPath::PathTracing`
- `PathTracingSettings` と runtime status
- SceneRenderer capture/apply と JSON serialization
- UI selector、support/status、disabled placeholder
- serialization / backward-compatibility tests

完了条件: Path Tracing を選択・保存・Evaluation Case 復元でき、未対応 device で安全に status を表示する。

### Commit 2: resources and RenderGraph boundary

- accumulation / scene-color specs と descriptors
- conditional history-clear pass
- PathTracing pass shell
- ToneMap source selection
- RenderGraph document/debug source registration
- Path Tracing 中に不要な raster/hybrid pass を除外

完了条件: RenderGraph に PathTracing pass/resources が現れ、clear color を PathTracing.SceneColor 経由で
tone map できる。

### Commit 3: primary rays and hit reconstruction

- `shaders_PathTracing.hlsl`
- `SceneRayQuery.hlsli` 抽出
- primary ray、closest hit、miss
- normal/albedo/emissive diagnostic output
- shader build entries for MSBuild and CMake

完了条件: DamagedHelmet と multi-mesh scene で geometry/material mapping が Hybrid Reflection と一致する。

### Commit 4: progressive accumulation

- deterministic RNG と pixel jitter
- accumulation read/write
- sample counter と reset key
- manual reset/pause UI
- resize / camera / scene / setting reset tests

完了条件: 静止 camera で convergence し、変更後の最初の frame に古い履歴が残らない。

### Commit 5: direct and diffuse indirect lighting

- directional-light visibility
- emissive/environment miss
- Lambert bounce loop
- initial path-tracing quality Evaluation Cases

完了条件: Cornell Box 相当 scene で direct shadow と diffuse bounce を確認できる。

### Commit 6: metallic-roughness PBR

- GGX importance sampling
- lobe selection/PDF
- normal map
- Russian roulette
- reference comparison captures

完了条件: metallic/roughness sphere と DamagedHelmet で expected material response を確認できる。

### Commit 7: validation and diagnostics

- CLI fixed-sample capture
- GPU timing / ray/sample diagnostics
- Debug Layer automation
- Evaluation Case と ROI を使った reference report
- 2080 と 4090 の performance/compatibility notes

完了条件: deterministic capture を再生成でき、Debug Layer error なしで比較 report を残せる。

## 12. Test matrix

| Area | Test |
|---|---|
| Build | Debug x64 MSBuild、SDK present/absent CMake configure where applicable |
| Shader | `shaders_PathTracing.hlsl` compiles as `cs_6_5` |
| Support | DXR tier below 1.1 cannot select active Path Tracing execution |
| Serialization | new settings round-trip and old JSON loads with defaults |
| RenderGraph | Path Tracing contains clear/path/tone-map; Deferred graph is unchanged |
| Resources | resize recreates both textures and resets history |
| History | camera, scene, material, light, seed, bounce changes reset exactly once |
| Post process | exposure/tone-map change does not reset accumulation |
| Geometry | indexed/non-indexed, multi-range, multi-instance hit reconstruction |
| Visual | normal, albedo, emissive diagnostics match existing scene data |
| Runtime | fixed seed/sample count capture is reproducible |
| D3D12 | no error in Debug Layer log during scene load, resize, path switching, capture |

## 13. Risks and decisions

### Accepted decisions

- Compute + Inline RayQuery is the first backend.
- Path Tracing is a third rendering path, not a Hybrid Reflection option.
- Dedicated scene-color and accumulation resources are used.
- Initial geometry is opaque only.
- Initial output is native render-size and does not use DLSS.
- Tone mapping remains shared.

### Risks to validate early

- Existing TLAS build flags and update behavior may not be optimal for long-running progressive rendering.
- upload-heap geometry buffers are simple but slower than default-heap buffers for repeated random access.
- bindless texture descriptor capacity may limit large glTF scenes.
- long accumulation in `R32G32B32A32_FLOAT` consumes significant memory at high resolution.
- current scene lighting does not yet provide an environment importance-sampling distribution.
- self-intersection bias must be tested across scene scales.

These risks should be measured after the primary-hit baseline works. They are not reasons to expand Commit 1 or 2.

## 14. Recommended immediate next task

Start with Commit 1 only. It establishes the persisted rendering-path contract and device-status UI without creating GPU
resources. Review that enum serialization, Evaluation Case restoration, and Path Tracing/DLSS mutual exclusion are
correct before implementing the RenderGraph boundary.
