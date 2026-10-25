# PT向けNRD/RR共通入力層: Work4設計

作成日: 2026-10-23 (Asia/Tokyo)

## 開始点と作業範囲

- workspace: `C:\work\RtPbrSurvey-work-4`
- branch: `codex/pt-nrd-rr-input-design-work4`
- 基点: main `91fbc2f` (PR #102)。NRD統合 `c5ae467` (PR #101) を含む。
- 目的: PT向けDLSS Ray Reconstruction導入に備え、NRDで得た信号・履歴の契約を整理し、共通部分とbackend専用変換の境界を設計する。
- 今回の成果物: 入力対応表、責務とデータフロー、未決事項、検証計画、実装PRの分割案。
- ランタイム変更、SDK導入、NRD調整、RR実行・画質判定は設計後の別工程。

NRDはexperimental/default-offのNative解像度PT previewとして統合済み。
PR #102でCornell/emissiveの限定した目視受入が記録された。
青いhighlightの粒状感はユーザー受入済みの既知制約であり、数値quality failureも残る。
入力層の整理を、NRD品質問題の修正や全シーン対応の証拠として扱わない。

## 2日・計10時間の進め方

| 日 | 時間枠 | 作業 | 完了条件 |
| --- | ---: | --- | --- |
| 1日目 | 1時間 | 統合済みPT/NRD/RRの棚卸し | producer、consumer、信号の意味、既存変換の一覧 |
| 1日目 | 1.5時間 | 固定SDK版の公式要件と照合 | 必須/任意入力、変換可能/追加生成必要/未確認の対応表 |
| 1日目 | 1.5時間 | 共通の信号・フレーム契約 | 単位、空間、sampling、背景、露出、履歴の定義 |
| 1日目 | 1時間 | 責務とデータフロー | 所有箇所、backend分岐、fallback境界 |
| 2日目 | 1.5時間 | backend専用変換を具体化 | 入出力形式、変換式、追加PT出力と未決事項 |
| 2日目 | 1時間 | RenderGraph接続設計 | 実行順、寿命、解像度、リサイズ、frame slot保護 |
| 2日目 | 1.5時間 | 検証と合格条件 | 数値契約、Native互換性、動作/品質の別判定 |
| 2日目 | 1時間 | レビューとPR分割 | 依存順、各PRの検証、RR試作への着手条件 |

時間は計画枠であり、実測作業時間や完了実績を表さない。

## 最初の棚卸し

ここでの名前は現行ソースの識別子。新しい公開API名の決定ではない。

| 入力・信号 | 現在のproducer/consumer | 共通化候補と注意点 |
| --- | --- | --- |
| 当該frameのradiance | `shaders_PathTracing.hlsl`: `g_currentFrameRadiance` | 蓄積済みNative画像と区別し、線形HDR、sample範囲、露出を記録する |
| 法線・roughness | 同: `g_normalRoughness` | world shading normalとroughnessの意味を明記。SDK packingは専用変換 |
| ViewZ | 同: `g_viewZ` | 正のcamera-axis距離。device depthやprimary ray Tと別の意味 |
| MotionVectors | 同: `g_motionVectors` | previous NDC minus current NDC。前回SDK評価frameとの対応も必要 |
| base color・材質 | 同: `g_albedo`, `g_materialF0Metallic` | base color、diffuse albedo、F0、metallicを混同しない |
| 物理成分radiance | 同: `g_physicalDiffuse`, `g_physicalSpecular` | NRD統合で追加済み。旧proposal分類のRadianceHitTと区別する |
| 発光等のstable成分・背景 | 同: `g_stableRadiance`, `g_backgroundRadiance` | フィルタ対象との分離と合成順を明記。RRへどう渡すかは公式契約との照合事項 |
| lobe距離・方向 | 同: `g_denoiserDiffuseDistance`, `g_denoiserSpecularDistance` と方向出力 | 距離、traced/miss/inactive/invalid等の状態、sampling方針を組で保持 |
| NRD材質係数 | `NrdRelaxInputConversion.hlsli`: `NRD_MaterialFactors` | SDK固有。共通材質データから生成し、NRD合成に使用する |
| NRD packing/admission | `shaders_NrdConversion.hlsl`, `NrdRelaxInputAdmission.hlsli` | RELAX専用。65504やSDK packingを共通の物理的距離定義に持ち込まない |
| NRD再構築・合成 | `shaders_NrdReconstruction.hlsl`, `shaders_NrdComposition.hlsl` | NRD側で再構築した距離を、RR入力へ自動転用しない |
| frame情報 | `Renderer/NrdRelaxSdkAdapter.h`: `NrdSdkFrame` | current/previous行列、寸法、評価index、resetの共通記述候補 |
| 既存RR入力 | `Renderer/StreamlineAdapter.h`: `RayReconstructionEvaluateInputs` | reflection固有の命名・resource構成がある。PT接続へそのまま流用する前に再設計 |

### SDK基準

- NRD: v4.17.3 / `792eff196afdd350fd9c3f862119017ccb438a0e`。
  正確な依存関係は `nrd-relax-dependency-manifest.json` を参照。
- RR: 現行MSBuild/CMakeの既定はStreamline 2.12.0。
  これを最初の仕様照合対象とする。実際に使うSDK/runtimeの版とhashは実装前に固定する。
- Work4の `third_party/` には確認時点で両SDKの既定ディレクトリが存在しない。
  NRDの参照は統合済みソースとmanifestに基づく。既存binをSDK導入済みの証拠にしない。
- Streamline固定tagの公式RR文書を取得して基本要件を確認済み。後述の具体設計と残る実機確認を参照。

## 責務分割の初案

```text
PT producer + camera/material/frame state
    |
    v
共通信号の記述 + 必要入力の存在/整合性確認
    |                                     |
    v                                     v
NRD専用変換                            PT用RR専用変換
係数・距離policy・packing・admission    SDK要件に合わせた材質/深度/MV等の変換
    |                                     |
    v                                     v
RELAX dispatch + NRD合成                RR evaluate + RR出力処理
    |                                     |
    +------------ Native A/B・診断 --------+
```

初期の共通層はrenderer所有の信号記述と検証を中心にする。
SDK型を公開インターフェースへ追加せず、不要な全画面copyや同一内容のGPU bufferを増やさない。
material factor、miss sentinel、距離再構築、SDK resource tagは各backendが所有する。

PT producerの物理信号、material情報、sampling metadataは共通記述の候補だが、
NRD用追加rayの生成を全backendで必須にするかは費用とRR要件を確認して判断する。

### 共通frame契約に含める候補

- current/previous cameraとprojection、行列storage/order、handedness。
- current/previous render rect、output rect、jitterとその単位。
- radianceの線形/露出状態、samples per frame、primary sampling方針。
- simulation frame IDとbackend evaluation ID、その前回評価frame。
- reset理由、resource再割当、pause/resume、backend切替。
- 各resourceのframe ID、format、寸法、有効性、意味のversion。

Native accumulationのリセットとbackend temporal historyのリセットは別状態として記述する。
NRDの連続camera履歴で得た方針を参考にしつつ、RRで履歴を保持できる条件は別途確認する。

## 次の照合で決める事項

1. RRに必要なnoisy color、diffuse/specular albedo、normal/roughness、depth、hit distanceの意味。
2. 既存RRの同解像度・reflection用evaluateを、PT全画像の初回試作へ適用できる範囲。
3. 1 sample/frame、pixel-center、jitter、NRD距離診断flagの依存をどう表すか。
4. primary miss、発光、Unlit、invalid lobe、未生成距離に対するbackendごとの扱い。
5. NRD用material demodulationや距離の加工を通さないRR入力の生成元。
6. 前回評価をskipした場合、camera cut、resize、露出変更、Native fallback後の再開条件。
7. NRD/Native/RRを同じscene/camera/seed/frame条件で撮影する検証インターフェース。

最初はNative解像度でのRR試作を候補とし、upscalingは入出力寸法の契約確定後に分ける。
これは設計案であり、採用SDKの対応を確認する前に実行可能とは判定しない。

## 検証設計の初案

- 共通信号の意味と値: finite、format、寸法、normal、ViewZ、MVの独立した数値確認。
- Native互換性: 変換層導入前後の同条件capture。新しいbinary/CSOのprovenanceを記録。
- NRD互換性: 現行packing/material-factor/reconstruction/compositionとhistoryテストを維持。
- RR動作: SDK evaluate成功と実出力使用を確認。fallback画像の保存成功とは別判定。
- 時系列: 静止、camera移動/停止、遮蔽解除、light/material変更、pause、resize、backend切替。
- 品質: Native高sample基準、固定ROIとscene、露出条件、画質閾値を撮影前に定義。
  NRDの既知失敗と目視受入は履歴として残し、RR評価の合格へ読み替えない。

## 実装PRの分割案

1. 共通入力契約とframe/resource記述。現行動作の互換性を確認。
2. NRD接続の整理。既存出力・設定・品質上の制約を保った移行検証。
3. PT向けRR入力変換とpreview/診断resource。evaluate前の数値契約を確認。
4. PT向けRR evaluate、履歴、Native fallback、同条件A/B。

PR境界と名前は2日目のレビューで確定する。

## 進捗

- [x] 最新mainからWork4専用ブランチを作成。
- [x] 統合済みNRDの受入範囲と既知制約を確認。
- [x] PT/NRD/既存RRの初期source inventoryと設計課題を記録。
- [x] 固定SDK版の公式要件と基本入力を照合。RRの残る実機確認は後述gateで管理。
- [x] 共通信号・frame契約とbackend別対応表のv0.1を作成。
- [x] RenderGraph接続と検証合格条件のv0.1を作成。
- [ ] 実装前レビューとPR分割の確定。

参照: `path-tracing-denoiser-input-contract.md`, `nrd-relax-input-semantics-audit.md`,
`nrd-six-hour-delivery.md`, `../../third-party/streamline-sdk.md`。

## 共通入力からbackend入力への具体設計 v0.1

以下は実装前の設計判断である。現行NRDの挙動は互換移行の基準とし、
RRの未検証項目には明示的なgateを置く。共通入力はresource viewと意味の記述で構成し、
すべてを新しい中間textureへcopyする設計にはしない。

### 1. 参照する公式契約

- [Streamline 2.12.0 RR guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/docs/ProgrammingGuideDLSS_RR.md): 4.1、4.2、5、6、7。
- [Streamline 2.12.0 constants](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/include/sl_consts.h): 行列、MV、jitter、reset。
- [Streamline 2.12.0 RR options](https://github.com/NVIDIA-RTX/Streamline/blob/v2.12.0/include/sl_dlss_d.h): exposure、normal packing、寸法。
- [固定NRD helper](https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/Shaders/NRD.hlsli): 材質係数とpacking。
- [固定NRD frame settings](https://github.com/NVIDIA-RTX/NRD/blob/792eff196afdd350fd9c3f862119017ccb438a0e/Include/NRDSettings.h): 行列、UV motion、history。

SDKの公開仕様と、このrendererでの採用案・検証済み挙動は分ける。
特にmiss時のRR距離、背景guide、jitterとprimary sampleの対応は、公開guideだけで完了扱いにしない。

### 2. 共通入力の意味

名称は設計上の論理名。型はrenderer所有とし、SDKの型やsentinelに依存しない。

| 論理入力 | 意味・単位 | 現行PTとの対応 |
| --- | --- | --- |
| FrameRadiance | 当該frameの平均、線形scene-referred RGB、露出適用前 | CurrentFrameRadiance |
| PhysicalDiffuse / PhysicalSpecular | 材質込みの物理成分、同じsample集合の平均 | PhysicalDiffuse / PhysicalSpecular |
| StableRadiance / BackgroundRadiance | primary emission等のstable項とprimary miss背景 | 同名出力 |
| ShadingNormal | world-space単位法線、hitのみ有効 | NormalRoughness.xyz |
| RoughnessR | 材質のroughness r。GGX alpha=r*rを別名で扱う | NormalRoughness.w |
| BaseColor / Metallic / F0 | 線形base color、metallic、normal-incidence specular reflectance | Albedo.rgb / MaterialF0Metallic.w / .rgb |
| PrimaryPosition / PrimaryViewZ | world位置と正のview-axis距離 | DenoiserPrimaryPosition / ViewZ |
| PrimaryMotionNdc | 同じ表面のprevious NDC minus current NDC、jitterを含まない | MotionVectors |
| SurfaceClass | Lit、Unlit、PrimaryMiss、Unsupported、Invalid | 既存admissionに関わる値を整理。専用material-class出力が必要かを実装時に決定 |
| LobeDistance[D/S] | primary surfaceから最初のsecondary surfaceまでのworld距離とray状態 | DenoiserDiffuse/SpecularDistanceと方向 |
| Sampling | frame/sample ID、batch数、primary位置方針、距離sampling policy ID | 既存flagsとcapture情報を記述化 |

`FrameRadiance = PhysicalDiffuse + PhysicalSpecular + StableRadiance + BackgroundRadiance`
を、対応するsample集合・debug modeで検証する。各成分の存在だけで恒等式成立とは判定しない。
最初はRadiance表示、opaque Lit、1 sample/frame、pixel-center primaryで整合性を固定する。
Native比較も同じprimary sampling設定で取得し、従来のstochastic画像とのbit一致を要求しない。

SurfaceClassは単なるradiance値から推測しない。金属でdiffuseが0のpixelをUnlitへ分類しない。
全入力は同一frame、同一render rect、同一sampling originに対応する必要がある。

### 3. 変換対応表

| 共通入力 | NRD RELAX向け | PT用RR向け採用案 |
| --- | --- | --- |
| FrameRadiance | Native fallback比較に保持 | 当該frameのnoisy HDRを直接color入力へ渡す |
| PhysicalDiffuse / Specular | 下記material factorで除算、RELAX helperでpack | 全体色に含まれる。NRD用demodulated画像をRR colorへ渡さない |
| Stable / Background | 現行compositionで明示加算、除外pixelはNative fallback | 初回案はFrameRadianceに含めて一度だけ渡す。RR出力へ再加算しない |
| Normal | 現行NRD encoding設定のhelper packing | world normal RGB。最初は既存adapterに合わせunpacked |
| RoughnessR | rをNRD packingへ渡す | rをR16_FLOATへ抽出。r*rへ置換しない |
| BaseColor / Metallic | diffuseAlbedo=BaseColor*(1-Metallic)をfactor計算へ | 同じdiffuseAlbedoを線形RGBA16_FLOATへ生成 |
| F0 / r / view方向 | NRD_MaterialFactorsのspecular factor | F0を種にview依存のspecular reflectanceを生成する候補。下記gateを参照 |
| PrimaryViewZ | 正のworld-unit depth、除外はNRD range policyへ | ViewZをlinear-depth tagへ接続する案を第一候補とする |
| PrimaryMotionNdc | 現行入力をalias、scale=(0.5,-0.5,0) | 共通UV変位へ変換しRG16_FLOAT。SDK scale=1、offset=0を候補として検証 |
| LobeDistance | D/S別の状態判定、必要時再構築、RELAX用packing | specularの実距離候補。miss/invalid/未生成は別gate |
| Primary camera hitT | guide比較専用 | specular hit distanceへ流用しない |
| Frame情報 | NrdSdkFrameへserialize | 既存Streamline camera constantsへserialize |

この表のRR列は新しいPT接続の採用案であり、既存Hybridの画質検証を移し替えたものではない。
既存Hybridのspecular-albedo shaderはF0を出力するが、PT版は公式guide 4.2.1の
view依存近似を候補として別に評価する。F0は共通入力、RR用reflectanceは派生値とする。
具体式は公式近似を実装するときに版を固定し、roughness引数とNoVを単独検証する。
NRDのspecular factorをRR用reflectanceと同じ値とは仮定しない。

### 4. NRD変換の契約

採用するD/S factorを `Fd`, `Fs` とすると、処理は次のとおり。

```text
共通材質 + normal + primary view方向
    -> 固定NRD helper -> Fd, Fs
物理Diffuse / Fd + diffuse距離 -> RELAX input D
物理Specular / Fs + specular距離 -> RELAX input S
既存reconstruction -> admission -> RELAX dispatch
出力D*Fd + 出力S*Fs + Stable + Background -> 合成候補
除外pixel / backend全体fallback -> 現行Native値
```

factor除算のfinite/range guard、inactive lobe、snapshot/range検証、
reconstructionとadmissionの順序、excluded pixelの書込制御は現行を維持する。
factorの最低値やmiss定数はNRD専用policy。共通層で勝手にclampして入力の問題を隠さない。
RELAXの距離は現行実装の未正規化距離。REBLUR式の正規化を追加しない。

### 5. RR変換の契約と不足情報

既存のStreamline初期化、device登録、support query、DLL管理、evaluate呼出しを再利用する。
PT用の入力bundleを追加し、reflectionEvaluatedRadiance等への依存を取り除いた境界を設ける。
Hybrid用の呼出しは互換wrapperに残す案とし、既存Hybridの入力・出力を変える移行は別検証する。

RR側では全体colorを再構成・demodulateせず、Common FrameRadianceをaliasするのが第一案。
必要formatに適合しなければ専用変換textureを割り当てる。
specular hit distanceを使う初回試作では、primary表面位置から測った実際のhit距離と
world/view行列を使う。独立にsampleされたNRD距離がRRの反射追跡に十分かは未検証であり、
そのまま共通の「RR-ready」flagを付けない。

| 距離の共通状態 | NRDの現行扱い | RRの初期方針 |
| --- | --- | --- |
| TracedHit | 有限距離として検査しpack | primary origin、幾何距離、specular方向の条件を満たすとき候補 |
| TracedMiss | scene範囲証拠付きでNRD専用値へ | sentinel未確定。RR対応試験完了まで未対応理由を返す |
| InactiveLobe | 専用zero表現と物理成分zeroの整合性検査 | diffuse inactiveはmetalの通常材質として表現。specular欠落と混同しない |
| InvalidDirection | NRD再構築の候補、失敗なら除外 | NRD再構築結果を自動転用せず、RR入力不足を返す |
| NotTraced / Invalid | 除外・fallback | evaluate前に拒否しNative fallback |

RRには現行NRD admission maskと同じpixel除外APIがあるとは仮定しない。
初回のRR数値試作は、画面を覆うopaque Lit面と有限specular hitを持つfixtureから始める。
対応外pixelがあるframeはwhole-frame Native fallbackとし、その理由とpixel数を記録する。
背景、Unlit、missを含む一般sceneへ広げる前に、それぞれのguide値と距離policyを検証する。
これらの未対応を0や65504で埋めてevaluateの成功だけを合格にしない。

初回はpreExposure=1、exposureScale=1とし、tone mappingをbackend出力後に一度適用する。
任意露出への拡張では、colorに掛けた倍率とSDK設定の式を対応表に追加する。

### 6. 深度・MV・行列の具体化

共通MVは現行のNDC差を保つ。SDK境界で必要なUV差は次式で求める。

```text
motionUv = motionNdc * (0.5, -0.5)
previousUv = currentUv + motionUv
```

NRDはraw NDC resourceと既存scaleを維持する。RR案ではUV値を書いたresourceを使い、
scaleを再度0.5倍しない。静止、右/上方向移動、camera orbit、物体移動で符号と大きさを検証する。
Streamline header/guideの正規化記述は抽象度が異なるため、式と投影の実測を合格条件とする。

RR depthは、同じprimary sampleのViewZを `LinearDepth` として渡す案を採用する。
既存adapterの `Depth` tag固定をinput enumで選択可能にする設計とする。
変更が困難ならprimary位置と同じprojectionでdevice depthを生成する代案があるが、
ViewZの数値だけをHW-depth tagへ渡すことは許可しない。

共通行列はengineのrow-vector数学規約で明記し、現在/前回のviewとprojectionを保持する。
NRDへのcolumn-vector/column-major、Streamlineへのrow-majorの変換は別helperに置く。
「transpose」という名前だけで契約を済ませず、world点を変換したclip座標の一致を検証する。
非対称projection、LH、near/far、orthographicをmetadataに記録する。

最初の両backend共通条件はpixel-center、projection jitter=0、1 sample/frame。
RRのjitter付きsamplingは別profileとして追加し、textureとcamera constantsを同時に変更する。
RRのDLAA/native解像度可否は既存設定だけで保証せず、optimal settings/support結果で判定する。
要求解像度が未対応なら明示拒否する。upscaling時にはoutput寸法をrender寸法から分離する。

### 7. インターフェースと所有箇所

実装時の候補ファイル:

- `Renderer/PathTracingReconstructionInputs.h`: SDK非依存の意味・resource view・frame記述。
- `Renderer/PathTracingReconstructionValidation.*`: 共通のformat/寸法/frame/samplingチェック。
- `Shaders/PathTracingReconstructionCommon.hlsli`: SDK非依存のUV/MV・材質導出等の小さい式。
- `Renderer/NrdRelax*` と既存NRD shaders: NRD専用変換・dispatch・合成。
- `Renderer/PathTracingRrInputPass.*`, `Shaders/shaders_PathTracingRrInputs.hlsl`: RR派生入力。
- `Renderer/StreamlineAdapter.cpp`: Streamline型、resource tag、SDK constants/evaluateの所有を継続。

共通bundleはCommonFrame、SurfaceInputs、RadianceInputs、OptionalDistanceInputsを持つ案とする。
backendごとにrequired capabilityを照合し、未生成resourceをnullableで明示する。
共通のReadyとNRDReady/RRReadyは別判定。失敗はreason enumとdiagnostic countersで返す。
Native PT通常利用に、NRD専用distance rayやSDK読み込みを強制しない。

### 8. RenderGraph・履歴・fallback

```text
PT producer -> common validation
    -> NRD conversion/reconstruction/admission -> RELAX -> NRD composition
    -> RR inputs -> RR evaluate -> RR output
Native / NRD / RRの選択 -> exposure/tone map -> presentation/capture
```

通常は選択backendだけを実行し、A/B campaignは同条件の別runで取得する。
同時実行のコストを画質比較の標準にしない。resourceはengine/rendergraph所有、
SDK内部履歴はadapter所有とし、descriptor/constantのframe slot再利用をfenceで保護する。
SDK呼出し後のcommand-list state復元とtagged-resourceの実state確認をpass境界で行う。

| イベント | 共通通知 | backendの初期方針 |
| --- | --- | --- |
| 初回、resize、format/backend変更 | resource generationとreset理由更新 | 履歴初期化。必要なら再割当 |
| camera poseの連続変更 | 前回frameとの対応を通知 | NRD既存継続policyを維持。RRは試験合格後に継続を許可 |
| cut、projection、scene/light/material変更 | 明示reset理由 | 両backend reset |
| pause | 新しいevaluateなし | 履歴を保持。再開時にMV基準との対応を検査 |
| 評価skip、fallbackから復帰 | last evaluated frameとの不一致 | 初期実装はreset |

共通producerのprevious matrixとbackendのlast evaluated matrixが一致しない場合、
その差を通常の連続motionとしてSDKへ渡さない。
Healthy-deviceの事前入力不足はNative fallback。GPU記録開始後の失敗やdevice lossは、
現行NRD同様に安全なfallback可否を別判断し、破損したcommand listで継続しない。

### 9. 実装前の合格条件

| Gate | 必要な確認 |
| --- | --- |
| C: 共通契約 | 全入力frame/rect/sample対応、finite/range、成分合計、MV/行列の独立計算 |
| N: NRD互換 | 現行guardとadmission、factor往復、packing、reconstruction/composition、history fixture |
| R1: RR入力 | albedo/roughness/depth/MV/distanceの期待値を単独capture。SDK未実行でも検査可能 |
| R2: RR実行 | 対応GPU/runtime、SDK成功、RR出力選択、Native fallbackとの区別、D3D12 errorなし |
| R3: 時系列 | 移動/停止、遮蔽解除、cut、編集、pause/resize/切替、背景・missへの拡張 |
| Q: 品質 | 固定scene/ROI/referenceと事前宣言した誤差条件。NRDの既知制約と独立して判定 |

今回の設計でC/N/R1の仕様を具体化した。RR距離miss、背景/Unlitガイド、
view依存specular albedo、jitter/native解像度の実機検証はR1/R2着手時の明示gate。
これらが未確認の間も、共通型と既存NRDの互換移行は先行できる。

## 最初の実装単位: 共通metadata契約

2026-10-23、同じWork4設計ブランチで実装を開始した。

- `Renderer/PathTracingReconstructionInputs.h/.cpp` に信号の識別子、共通frame、
  resource metadata、検証結果と失敗対象の信号名を追加。
- v1は1 sample/frame、pixel-center、projection jitter=0、露出倍率1。
  render/output寸法は独立して保持するが、upscalingのSDK対応を保証しない。
- 必須共通入力は当該frame色、normal/roughness、ViewZ、MV、base color、F0/metallic。
  physical/stable/background/距離等は追加capabilityとして記述可能。
  各backend側で追加の必須入力を検査する設計を維持する。
- presentな全入力のformat、render寸法、frame ID、sample開始位置・batch数、
  sampling policyのidentityを検査する。別frameや別sampleのguide混在を拒否する。
- camera行列のfinite/singular検査とNDC差からtop-down UV差への変換関数を追加。
  行列の投影上の意味やGPU画素内容の検証は別契約。
- CMakeとMSBuildのsource一覧、Visual Studio filtersへ登録した。

検証:

- 新しいCMake構成 `build/reconstruction-input-validation` の生成成功。
  ローカルWork4の古い依存セットにtinyexrが無いため、構成時だけ既存Work3の
  `build/vcpkg_installed/x64-windows` をread-onlyで参照した。
  Work3のソース・build設定は変更していない。
- Debug x64の独立したCPU契約test targetのビルド成功、warning/error 0。
- CTest `RtPbrSurvey.PathTracingReconstructionInputs`: 1/1成功。
  全15信号に対するstale frame、寸法、sample、policy、formatの拒否、
  必須/任意入力の区別、sampling/exposure制約、singular行列とperspective行列、
  独立した投影例によるMV符号・倍率を確認。
- ビルドログ: `C:/work/RtPbrSurvey-agents/pt-common-input-contract-work4`。

今回のpassはCPU metadata検査である。現時点でengineのproducerからmetadataを
構築する処理、実GPU resource binding、NRD/RR readiness検査、SDK変換passは未接続。
既存rendererの実行経路は変更していない。app全体やGPU画質の新しい合格を意味しない。

次の実装単位は、PT producerでのprovenance記録と実resourceのGetDescを基に
bundleを構築し、現行NRDの専用gateへ接続すること。metadataをconsumerで
現在frameへ一律に付け直して、stale-resource検出を無効化しない。

## 次の実装単位: producer記録と実resource照合

2026-10-24、最初の実装単位を `92edcdb` としてcommitし、次へ進んだ。

- `Renderer/PathTracingReconstructionResources.h/.cpp` を追加。
  PT dispatch記録直後に、実resourceのGetDescとframe/sample/policy情報を記録する。
  ComPtrでallocationの同一性を保持し、consumer側でstampを書き換えない。
- NRD変換diagnostics構成でengineから15信号を対応付け、変換直前に検査する。
  共通metadataに加えてallocationの同一性、2D・単一array/mip/sample、SRV許可を確認。
  不整合は新しいhost gate 12として既存の変換除外経路へ渡す。
- 距離diagnostics無効時は、距離・方向・primary位置を未生成capabilityとして扱う。
  既存のsampling gate 8、未生成距離gate 10等の優先順位を維持する。
- render pass構築時にproduction記録を無効化する。pause/skip/unsupported dispatchから
  前回のstampを引き継がない。frame IDはNative sample indexと独立して増加する。
- 共通camera情報のprevious値を、producerがMV計算で実際に使うprevious ViewProjectionへ
  具体化した。engineのshader用transposeを戻して保存し、前回viewとprojectionを推測して
  分離しない。各backendの分離したprevious行列はそのbackendのhistoryが保持する。

検証結果:

- NRD_CONVERSION_DIAGNOSTICSを有効にしたDebug x64アプリと2つの契約test targetを
  ビルド。warning/error 0。NRD SDK本体のdispatchはこの構成では有効にしていない。
- CTest共通metadata/実resource契約: 2/2成功。
  実resourceテストはD3D12 WARPでtextureを生成し、allocation差替え、削除、resize、
  array誤用、missing production、stale frame、無効化後の再利用拒否を確認した。
- 既存 `15-nrd-material-matrix` のSceneFileとpresetを使い、30 warm-up frames後に
  `PathTracing.NrdConversionDiagnostics` を1920x1080でraw capture。
  sampleStartIndex=30、全値finite、code 0が248,050画素、code 7が15,374画素、
  primary missのcode 4が1,810,176画素。共通入力失敗gate 12は0画素。
  code 7は再構築を必要とする候補であり、最終NRD admission成功数とは区別する。
- D3D12 ERROR/CORRUPTION 0、既知buffer initial-state warning 2。
  初回の非同期起動は完了captureを残さなかったため採用せず、process終了を待った
  再実行のexit 0と完成したPTBUF/logを確認した。
- ログ: `C:/work/RtPbrSurvey-agents/pt-common-binding-work4`。
  capture: `build/reconstruction-input-validation/captures/common-binding-20261024`。
- 依存は前段同様にWork3の既存vcpkgをread-only参照し、固定NRD helper sourceも
  Work3の `third_party/nrd-sdk-4.17.3` をread-only参照した。

既存NRD shaderの式、packing、sampler、履歴policyはこの単位では変更していない。
この検証は共通入力検査とNRD変換入口までであり、RELAX dispatch/compositionの
新しい回帰結果、画質改善、Native画像のbit一致、RR対応を主張しない。
次はbackend追加capabilityとNRD専用変換の境界を共通bundleへ整理し、SDK有効構成で
現行NRDの回帰を確認する。RR派生入力passはその後の独立単位とする。

## NRD専用capability境界とSDK有効回帰

2026-10-24、NRD用入力要件を `Renderer/NrdReconstructionInputs.h/.cpp` に追加した。

- 共通metadata合格とNRD用追加capabilityを別判定にする。
  `MetadataReady()` はSDK対応・GPU画素admission・画質の合格を表さない。
- conversion-only profileは11信号を必須とし、SDK合成profileはstable/backgroundも要求する。
  共通契約不整合はその理由を保持し、欠落したNRD信号は識別子付きで返す。
- conversion shaderのt0..t10を `kNrdRelaxConversionSignals` の順序で定義し、
  engineのroot signatureはその共通semanticからnative guide indexへ変換して結合する。
  GetDesc検査とdescriptor layoutが同じmappingを参照する。
- NRD固有の必須信号不足はhost gate 13。既存のpixel状態、距離再構築、packing、
  material-factor往復、RELAX設定・dispatch・合成式は既存処理を使用する。
- 新しいtestは実際のHLSL resource宣言を読み、registerと共通semanticを照合する。
  必須信号ごとの欠落、合成profileの追加要件、共通provenance不整合の保持も確認する。

SDK有効の独立buildで回帰した。固定NRD/MathLib/ShaderMakeはWork3のcheckoutを
read-only参照し、生成物と実行ファイルはWork4の専用buildへ出力した。

- Debug x64: appと関連testのビルド成功、error 0。
  NrdRelaxAdapterTestsのlinkで既存のunused d3d12 delay-load警告 LNK4199が1件。
- CTest: 共通metadata、実resource、NRD入力、既存NRD adapter、SDK planningの5/5成功。
- GPU dispatch/history runnerのPython unit tests: 11/11成功。
- 2つのrunnerへ `--exe` と `--shader-directory` を追加した。
  defaultは従来のbuild配下を維持し、実際に指定したexe/27 SDK shadersのhashを記録する。

既存 `nrd-step2s-material-cohort-20261013` のscene/presetをWork4のignored作業領域へcopyし、
その固定fixtureを使用した。元のWork3 cohort、ソース、既存exeは変更していない。

| 回帰 | 結果 | 範囲 |
| --- | --- | --- |
| RELAX GPU dispatch | 9/9 captures成功 | D/S入力・admission・出力、fresh-process再現、距離diagnostics無効時の除外 |
| Diffuse output lifecycle | 19/19 checkpoints成功 | pause/reset/resume、sampling非互換、camera/light/material/geometry/resizeの既存timeline |
| Specular output lifecycle | 19/19 checkpoints成功 | 同上 |
| NRD scene-color lifecycle | 19/19 checkpoints成功 | 同上とNRD Preview/fallback状態のmetadata検査 |

GPU dispatchではD/Sとも1,286,774画素のadmissionと非passthrough出力を確認。
再実行は両出力でbit一致し、除外pixelへの書込・無効条件の残留出力は認めなかった。
履歴timelineの全57 capturesで既存のexact comparisonと状態条件が合格した。
全66 capturesのログでD3D12 ERROR/CORRUPTION 0。

実行exe SHA256: `493de835e8814207ca43a02366cfc5188bebdd877ad5d1516b98f890e1a2abd5`。
sourceは `92edcdb` に対する未commit差分を含む。
raw reportsは次に保存し、exe/script/SDK shaderの実行時hashを保持した。

- `build/reconstruction-input-validation/captures/nrd-common-dispatch-20261024/report.json`
- `build/reconstruction-input-validation/captures/nrd-common-history-20261024/report.json`
- build logs: `C:/work/RtPbrSurvey-agents/pt-common-nrd-sdk-work4`

これは現行の数値・履歴契約の回帰であり、統合前exeとの新しいbit一致比較、
glossy画質の改善、連続移動/disocclusion全般、別GPU、RR接続の合格ではない。
Native/NRD/RRで共有するmetadataとNRD用の結合境界ができたため、次の独立単位は
RR用派生入力passとその数値契約を実装すること。SDK実行は入力検査の後段とする。

### RR用派生入力の単独変換（Work4）

`Shaders/RayReconstructionInputConversion.hlsli` にSDK非依存の数値変換を追加し、
`Shaders/shaders_PathTracingRrConversion.hlsl` にtexture入出力のcompute passを用意した。
この段階ではengineにscheduleせず、既存Hybrid RR evaluateも変更しない。

| 入力 | RR向け派生値 |
| --- | --- |
| FrameRadiance | 現在フレームのscene-linear RGBをそのまま保持。累積、demodulation、stable/background再加算なし |
| BaseColor / Metallic | diffuse albedo = BaseColor * (1 - Metallic) |
| F0 / material roughness / NdotV | Streamline 2.12.0 guideのEnvBRDFApprox2。引数alphaはr*r、roughness出力自体はr |
| NormalRoughness | 正規化world normalと別roughness texture |
| ViewZ | 正のLinearDepth。HW depthへ誤tagしない |
| MotionVectors | previousNDC-currentNDCに(0.5,-0.5)を掛けてtop-down UVへ変換 |
| SpecularDistance | TracedHitの幾何距離xを保持。query TであるzやNRDの65504 miss値に置換しない |

texture passのt0..t7はFrameRadiance、NormalRoughness、ViewZ、MotionVectors、
BaseColor、MaterialF0Metallic、PrimaryPosition、SpecularDistanceの順。
u0..u8はcolor、diffuse albedo、specular albedo、normal、roughness、LinearDepth、
motion、specular hit distance、uint statusの順。
定数はrow-major inverseViewProjectionとrender width/heightであり、pixel-centerのnear/far点から
view方向を求める。cameraPositionからの方向に固定せず、orthographicも表現できる式とした。
このtexture bindingとcamera再構築のGPU実行は、後述する数値helper検査の対象には含まれない。

statusは0=numeric candidate、1=primary不支持、2=material不正、3=geometry/depth/MV不正、
4=radiance不正、5=distance不支持。不支持pixelの出力は全channelを0にする。
これはRRへ0を渡してよいという規則ではなく、将来のwhole-frame fallback判定用である。
背景、Unlitに由来するuntraced、miss、inactive、invalid distanceはcandidateにしない。
raw TracedHitがあってもNRD用独立sampleとRR radianceの相関は未認定なので、
status 0のみでSDK evaluateを許可してはならない。

単独検証の出力はfloat32で、70000のHDR値と幾何距離を保持するケースも含む。
runtimeのallocation、RR受け入れformat、halfへの変換とoverflow policyは別途確定する。
texture passはfloat型UAVを宣言するだけで、そのformatを選択・保証しない。

`Tests/shaders_RrInputConversion.hlsl` は共通helperを32ケースで実行し、
`Tests/RrInputConversionValidation.h` は展開したdouble精度多項式と明示的な期待値で比較する。
非金属・金属・部分金属・黒・粗さ0/1・grazing・normal正規化・MV符号とscale、
NaN/Inf・不正material/normal/view/depth・距離stateの除外を検査する。
既存D3D12 test harnessの新modeを使用し、NRDのtest modeと出力strideを分離した。
2026-10-24の検証結果:

- Debug app / 関連targetのbuild成功、warning 0、error 0。
  logs: `C:/work/RtPbrSurvey-agents/pt-rr-input-conversion-work4`。
- texture passとtest shaderのDXC cs_6_0 compile成功。
- 共通入力、resource binding、NRD境界、sampler/packing/material/conversion、SDK adapterと
  RR変換のfocused CTestは10/10合格。
- RR数値変換32ケースはWARPとhardware GPUの両方で合格。
  各実行は既存harnessの28,672要素に32ケースを反復し、D3D12 ERROR/CORRUPTION 0。
- 今回はNRD runtimeのshader/dispatch式を変更していないため、直前の66-capture回帰を
  再実行していない。RR texture passのGPU dispatchと実capture照合も今回の合格範囲に含めない。

次はtexture passのresource/provenance結合、実captureでの変換照合、whole-frame gate、
ray/radiance相関とunsupported pixel policyの認定を行う。
その後にPT用RR adapterのoutput extent / LinearDepth tag / constants / historyを接続する。

### RR texture replayと全画素判定（Work4）

`Renderer/PathTracingRrInputs.h/.cpp` でt0..t7の共通semanticとRR用の必須入力を定義した。
共通metadata不整合は元の理由を保持し、PrimaryPosition / SpecularDistanceの追加欠落も識別する。
NRDのPhysicalDiffuse/Specular、方向、Stable/BackgroundはこのRR変換には要求しない。

`InspectPathTracingRrFrame` はmetadataとは別に、GPU処理・readback完了後のstatus集計を検査する。
frameId、samplingSignature、sampleStartIndex、render extentが一致する集計だけを受け入れる。
全statusの合計が全画素数に一致し、全画素がstatus 0の場合のみ数値上のcandidateとする。
合計は残数からの減算で確認し、uint64 overflowで全画素に見える値も除外する。
未完了、古い結果、部分ROI、過剰count、不支持pixel 1個以上は失敗となる。
この関数を呼ぶ前のresource identity / provenance照合は共通production層の責務である。
これはSDK実行許可ではなく、engine scheduling / readback経路にはまだ接続していない。
この集計はoffline / 診断用であり、過去frameの結果で現在frameの実行を許可してはならない。
将来のlive接続では同一frameの結果待ちに伴う同期と通常描画のstall回避も設計する。

`Tests/RrTextureConversionTests.cpp` は実際の `shaders_PathTracingRrConversion.hlsl` を
8 SRV / 9 UAV / row-major camera constantsでdispatchし、全出力をreadbackする。
検証用inputはfloat32に揃え、statusはR32_UINT、guideはRGBA32/RG32/R32_FLOATで実行する。
元のraw captureに含まれるhalf値は復号した値を保ち、SRV bindingそのものはfloat32で検査する。
runtimeのnative half SRVや最終RR formatを認定する試験ではない。

`Tests/PathTracing/validate_rr_texture_conversion.py` はdouble精度の独立referenceと9出力を比較する。
13x9の端数extentでrow pitchとdispatch端を含めて検査し、正投影、レンズシフト付き透視投影、
不正cameraを実行する。MVは画素位置ごとに変え、縦横・scale・読み取り位置の誤りを検出する。
実PT入力は同じscene/presetのfresh-process capture 8枚で構成し、resource/format以外のmetadataが
完全一致するcohortだけを再生する。行列の転置規約、sample profile、formatも確認する。
このcohortは再現性に依存するoffline照合で、同一live dispatchのresource provenanceの代用ではない。
F0/MetallicもnativeではRGBA16_FLOATである。参照値は保存済みF0/Metallicを使用し、
BaseColorからF0を作り直したり量子化前のmaterial値へ差し替えたりしない。

単独実行の例（NumPyを使えるPython、repo rootから）:

```powershell
python -B Tests/PathTracing/validate_rr_texture_conversion.py `
  --test-exe build/reconstruction-input-validation/Debug/RtPbrSurvey.RrTextureConversionTests.exe `
  --shader build/reconstruction-input-validation/Tests/shaders_PathTracingRrConversion_CSMain.cso `
  --output build/reconstruction-input-validation/captures/rr-textures-new
```

`--hardware` で実GPUに切り替える。さらに `--source <scene.json/preset.jsonを含むfolder>` と
`--exe <RtPbrSurvey.exe>` を指定すると、fresh captureから再生まで連続実行する。
出力folderは空の場所を指定する。reportはexe / producer shader / converter shader / script /
raw buffer / outputのhashを記録し、実行scriptのsnapshotも出力先へ保存する。

2026-10-24の検証結果:

- Debug build成功、warning 0、error 0。
  logs: `C:/work/RtPbrSurvey-agents/pt-rr-texture-replay-work4-fix`。
- metadata / resource / NRD境界 / RR全画素判定 / sampler / NRD・RR変換のfocused CTestは7/7合格。
  RR判定では必須信号欠落、古いframe/seed-policy/sample/extent、未完了、部分coverage、
  count overflow、不支持status各種が1画素だけある場合も確認した。
- 実texture passはWARPとhardwareで3 camera条件すべて合格。
  正投影・off-axis透視投影の最大絶対誤差は6e-8未満、不正cameraは全出力の数値が0。
- fresh capture 8枚の全metadataが一致し、sampleStartIndex=30、1920x1061の全2,037,120画素を
  実GPUで再生した。9出力が独立referenceと合格し、最大絶対誤差は7.750593766786551e-8。
  color、roughness、LinearDepth、motion、距離は誤差0で、albedo/normalのみfloat演算の差を認めた。
- capture全8件とtexture dispatch全件でD3D12 ERROR/CORRUPTION 0。
  capture起動時の既知のbuffer initial-state WARNING 2件/実行は残る。
- 実captureのstatus集計は `[175831, 750346, 0, 0, 0, 1110943]`。
  175,831画素だけがnumeric candidateで、750,346画素がprimary不支持、1,110,943画素がdistance不支持。
  全画素がcandidateではないため、このframeは除外対象である。
  この結果を理由に背景やmissを0距離としてRRへ通す変更は加えていない。

raw reports:

- `build/reconstruction-input-validation/captures/rr-textures-warp-20261024-final/report.json`
- `build/reconstruction-input-validation/captures/rr-textures-captured-20261024-final/report.json`

各folderの `validation-script.py` は実行時のhashに対応するsnapshot。
最初のcapture試行では検証側がMaterialF0MetallicをRGBA32と想定して中断したため、
実際のRGBA16 formatに修正し、上記final cohortで8枚すべてを新たに取得した。

次はlive engine上のresource/provenance結合と診断captureへの接続、ray/radiance相関および
unsupported pixel policyの認定。その後にPT用RR adapterのextent / LinearDepth / historyを接続する。

### Live RR入力変換の診断接続（Work4）

`RTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS` を追加した。defaultはOFFで、ON時のみ
PT dispatch直後に `PT RR Input Conversion Diagnostics` passを実行する。
NRDのconversion / SDK optionとは独立で、shader buildを必須にしている。
通常のNative PT表示・積算と既存Hybrid RR evaluateは変更しない。

```powershell
cmake -S . -B build/reconstruction-input-validation `
  -DRTPBRSURVEY_RR_CONVERSION_DIAGNOSTICS=ON
```

live passは共通semanticのt0..t7から実SRV descriptor offsetを導出する。
PT dispatchの記録直後にGetDescとallocation identityをstampし、同じproduction frameに対して
共通metadata検査とRR必須入力検査を行う。検査失敗時はshaderのhost gateを6にし、
raw入力を読まずに全guideを0、statusを6へ書く。1spf / pixel-center / jitter 0 / exposure 1の
既存v1 profileを緩和しない。camera定数はproducerが実際に使用したinvViewProjectionを
row-majorへ戻して渡す。

出力は `PathTracing.RrColor`、`RrDiffuseAlbedo`、`RrSpecularAlbedo`、`RrNormal`、
`RrRoughness`、`RrLinearDepth`、`RrMotion`、`RrHitDistance`、`RrStatus`。
先頭4つはRGBA32_FLOAT、motionはRG32_FLOAT、残りはR32_FLOATとし、既存debug preview / raw PTBUFで参照する。
live版statusはR32_FLOATに整数0..6を保存し、standalone GPU試験のR32_UINTとの型の違いを明示した。
raw PTBUF writerとPython readerにRG32_FLOATを追加した。RR SDK用の最終format認定は別工程である。

BuildRenderPassesの開始で `recorded=false / hostGate=6` に戻す。
paused frameは出力を保持しても新規変換と表示せず、history clear時はstatusを6で消去する。
capture metadataの `rrConversion` はrecorded、host gate、production frameId、producer sampleStartIndexを記録する。
pause時の表示sample countと、最後に変換したsampleStartIndexは別の値として保持する。

RrStatusのraw captureだけは既存screenshot fenceの完了後に全画素を集計し、
`InspectPathTracingRrFrame` へ渡す。集計に使用する入力metadataとrecorded flagはcopy command記録時に
capture slotへsnapshotする。readback完了時の新しいlive frameへ置き換えない。
結果の `rrNumericFrame` には7 statusのcounts、unknown count、completed、frameId、sampleStartIndex、
numeric gateの合否とissueを保存する。Native evaluation qualificationは常にfalseである。
通常描画の毎frame readbackや待機を追加せず、capture要求があった場合だけ既存の非同期readbackを使う。

`Tests/PathTracing/validate_rr_live_conversion.py` で、raw 8入力とlive 9出力のcohort照合、
独立referenceとの全画素比較、4spfとdistance-offのhost gate、既存19-stage timelineを検証する。
timelineはcheckpointをpauseしてcaptureするため、新しいproductionとして認定されないことも確認する。
2026-10-24の検証結果:

- RR+NRD診断構成とRR単独構成のDebug buildに成功。ともにwarning 0、error 0。
  RR単独はNRD関連optionとBUILD_TESTSをOFFにしても起動・変換・status captureが成功した。
- 共通入力、resource binding、NRD境界、RR gateとshader helperのfocused CTestは11/11合格。
- live 8入力 / 9出力の17 captureで、全metadataのcohort一致と1920x1080の全画素照合に合格。
  最大絶対誤差はspecular albedoの7.733409745913633e-8。
  color / diffuse albedo / roughness / LinearDepth / motion / hit distanceは誤差0。
- statusは `[182106, 748652, 0, 0, 0, 1142842, 0]`、unknown 0。
  CPU側のcapture完了後集計も一致し、frameId=31、sampleStartIndex=30で数値上の全画素gateは失敗する。
  不支持pixelがあるための正しい除外であり、nativeEvaluationQualifiedはfalse。
- 4spfのstatus全6とcolor全0、distance diagnostics OFFのstatus全6を追加3 captureで確認。
- 19-stage timelineは全checkpointと8組のexact replay比較に合格。
  pause中はrecorded=false / numeric gate失敗を保持し、reset-paused、4spf、3spfはstatus全6。
  camera / light / material / geometry / resize後のfresh比較も合格した。
- RR単独のRrStatus captureも上記status集計と一致した。
  RR側の全39 captureとRR単独captureでD3D12 ERROR/CORRUPTION 0。

build logs:

- `C:/work/RtPbrSurvey-agents/pt-rr-live-conversion-work4`
- `C:/work/RtPbrSurvey-agents/pt-rr-live-only-work4`

raw reports:

- `build/reconstruction-input-validation/captures/rr-live-conversion-20261024/report.json`
- `build/rr-live-only-validation/captures/rr-only-smoke-20261024/report.json`

RR+NRD exe SHA256は `98acf3d4c8bf2ef991b72eaecd3d72524d13aa82e68c0a10f2d9ae9212a1c3c2`。
Native PT shader SHA256は従来と同じ `23e06232a1a6454ac38b6512741355b133029dfd1d2f59002b2bfd48b9b0ca0c`。
NRD SDK、Native PT shader、推定式、sampling、NRD dispatch設定をこのlive接続で変更していない。

NRD併用回帰で残った再現性課題:

- D/Sとも1,324,948 pixelでadmissionと非passthrough出力を確認。
  Diffuseのfresh-process repeatはbit一致したが、Specular repeatは580 pixelで不一致。
  RGB最大差は0.0005791187286376953、alpha最大差は20。差の範囲はx=772..875、y=0..68。
  このため `validate_nrd_gpu_dispatch.py` のstrict replay判定はfailedのまま保存した。
- 追加captureのNrdInputSpecularは最初の入力とbit一致。
  追加Specular outputは最初のoutputとは不一致で、repeat側のoutputとはbit一致した。
- RR診断をOFFにした別Debug buildでも同じfixture / 1920x1080で比較した。
  Specular inputはRR有効構成とbit一致したが、OFF構成内のfresh-process output 2枚にも
  2,006 pixelの再現差があり、RGB最大差は0.0005471706390380859、alpha最大差は20。
  RR有効化に固有の差ではないことを確認した。SDK内部と履歴入力系列の原因は未特定である。
- distance diagnostics OFFのDiffuse / Specular出力はともに全0を確認した。
  上記NRD captureでD3D12 ERROR/CORRUPTIONは検出していない。
  bit一致基準の緩和や、NRDの画質・推定式への変更は行わない。

関連reportsと比較build log:

- `build/reconstruction-input-validation/captures/nrd-after-live-rr-20261024/report.json`（strict replay failed）
- `build/reconstruction-input-validation/captures/nrd-rr-determinism-audit-20261024/report.json`
- `build/nrd-rr-off-validation/captures/determinism-20261024/report.json`
- `build/reconstruction-input-validation/captures/nrd-rr-excluded-specular-20261024/report.json`
- `C:/work/RtPbrSurvey-agents/pt-nrd-rr-off-audit-work4`（RR OFFのDebug build成功、warning/error 0）

次はRRのspecular距離とnoisy radianceの対応、背景 / miss / untraced policyを認定する。
NRDのstrict replay問題は上記の再現資料で独立して調査する。
PT向けRR adapterのLinearDepth tag / output extent / history / native evaluate接続は未着手である。

## RR距離入力の経路対応と採用方針

現行の `DenoiserSpecularDistance` はNRD診断用の独立GGX probeであり、
noisy radianceを生成した実際のprimary continuationとは異なる乱数系列である。
したがって、finite / positive / TracedHitを満たすstatus 0でも、RRへの採用は認定しない。
これは変換数値の不具合ではなく、信号の意味と用途の境界である。

実際の経路は既存の `PrimaryContinuation`、`SecondaryRayData`、
`SecondarySurfaceDistance` で観測できる。
`SecondaryRayData.y` の1/2はdiffuse/specularのproposal選択であり、物理成分の識別ではない。
実際のsample方向でdiffuse / specularのBRDFを両方評価し、後続radianceを両成分へ配分するため、
diffuse proposalを捨てるとphysical specularに寄与する経路も捨てる。
単純にproposal 2の距離だけをRRへ渡す設計は採用しない。

| 入力状態 | 現在の扱い | 次の認定条件 |
| --- | --- | --- |
| 独立NRD probeのfinite hit | 数値診断専用 | 実radianceとの対応がないため、そのまま昇格しない |
| 実continuationのfinite hit | 次の距離候補 | surface origin補正、成分との対応、複数寄与の扱いを検証 |
| primary background | whole-frame除外 | depth / normal / albedo / MVを含む背景契約を確立 |
| continuation miss | whole-frame除外 | RR用のmiss表現を一次資料または公式sampleで確認 |
| untraced / inactive / invalid direction | whole-frame除外 | missと混同せず、発生理由とfallbackを定義 |
| specular motion vectorによる代替 | 未実装 | dense reflected geometry motionと動的物体の検証 |

インストール済みStreamline 2.12.0の `ProgrammingGuideDLSS_RR.md` 4.1.8/4.1.9では、
reflected specular MVの代わりにprimary surfaceからのworld-space hit distanceを渡せる。
ただし背景 / miss / untracedに対するpixel sentinelの規定は当該節にない。
NRD用の65504、RayQueryのrayTMax、診断用の0をRRの正しいsentinelとして転用しない。
pluginの `getTaggedResource` と必須入力判定で距離/MV tagがoptionalでも、
両方省略した場合の意味や画質が認定されるわけではない。

次の実装単位は、実continuationを共通入力のoptional信号として公開し、
初期認定profileを1spf / pixel-center / opaque / 2bounce / BSDF-only環境に限定して、
finite hitから距離の対応を検証することとする。
背景・missを含むframeを送るnative RR接続は、その契約の確認後に進める。
既存のHybrid RRとNRD変換は変更しない。

経路対応の実測（2026-10-24）:

- `Tests/PathTracing/validate_rr_distance_lineage.py` を追加し、同一設定のfresh processで
  実経路4信号、NRD specular direction / distance、PhysicalSpecularの計7枚をcaptureした。
  各captureのresource / format / 全metadataのcohort一致とfinite値を確認する。
- 比較可能な1,130,372 pixelすべてで実continuationと独立probeの方向差が1e-4を超えた。
  diffuse proposalかつPhysicalSpecularが非zeroのpixelは744,769だった。
- 実経路がuntracedでもprobeがhitとなるpixelは10,557、
  実経路がmissでもprobeがhitとなるpixelは113,846だった。
  probeのhit有無を実radianceの経路状態として流用できないことも確認した。
- 既存 `validate_secondary_geometry.assess` の独立平面交差oracleも同じcaptureに適用した。
  status / proposal / 単位方向 / pdf / primary validityは合格したが、
  queryTとsurface distanceは事前固定の `1e-4 + 5e-5 * expected distance` を超えた。
  hit 9,279 / miss 66,608のうち、最大queryT差は0.0014515573671274318、
  最大published surface distance差は0.0014514341380174045、最大許容比は8.31928331595752。
  原因は未特定であり、float精度だけが原因とも断定しない。
  oracleの許容誤差を緩和せず、report全体はfailedとして保持した。
- D3D12 ERROR/CORRUPTIONは7 captureとも0。
  producer shader / exeは前節のlive検証と同一で、renderer変更や再buildは行っていない。

raw report:
`build/reconstruction-input-validation/captures/rr-distance-lineage-20261024/report.json`

距離候補の認定前に、上記交差誤差をpixel座標・grazing角・GPU ray生成精度から切り分ける。
このauditは独立probeの不適合を実証するものであり、actual continuationのRR採用を認定しない。

## 距離差の切り分け: 一次レイ計算精度（2026-10-24）

前節の距離差を `validate_rr_distance_precision.py` で切り分けた。
元の7 captureは変更せず、同一exe / producer shader hashのfresh processから
`DenoiserPrimaryPosition` を1枚追加captureした。
全metadataを比較し、frame / sample / camera / size / policyが一致することを確認する。

一次ヒット位置の3モデルから同じ記録済みcontinuation方向を使い、
有限平面との二次交差をdoubleで独立計算した。
surface distanceは、bias加算後の実origin offsetを含めた幾何距離として別途照合する。
元のdouble oracleも同一reportに残し、過去のfailed reportは上書きしない。

| 一次ヒット位置のモデル | queryT不合格数 | 最大queryT差 | 最大surface距離差 | 最大許容比 |
| --- | ---: | ---: | ---: | ---: |
| 従来のdouble unprojection | 70 | 0.0014515573671274318 | 0.0014514341380174045 | 8.31928331595752 |
| float32 unprojection / normalizeモデル | 0 | 0.00006513425274690121 | 0.00006529143103950474 | 0.23674440682907744 |
| GPU記録済み一次ヒット位置 | 0 | 0.0000033142851189049694 | 0.0000033211708667657547 | 0.024495358851696666 |

3モデルとも非境界hitは9,279 pixelで、許容誤差は従来の
`1e-4 + 5e-5 * expected distance` のままである。
status、primary validity、単位方向、pdf、proposal等の従来チェックも合格した。
GPU位置とdouble位置の最大成分差は0.00008273036733630335。
GPU位置とfloat32モデルの最大成分差は0.000030517578125。

例えばpixel (1192, 440) では、一次ヒット位置のz差が約0.0000544139、
二次方向と壁面normalの内積が約-0.0374206となる。
浅い角度の交差では、この起点差が距離へ増幅される。
同pixelのqueryT差0.00145156はこの増幅と整合する。
`MakePrimaryRay` はfloatのunprojection / divide / normalizeを行うのに対し、
従来oracleはその経路をdoubleで再構成していた。
このfixtureで観測した大きな距離差は、一次起点の数値差で説明でき、
二次queryTやsurface距離のbias補正式の不具合を示すものではない。

ただし、GPU記録位置を使うチェックは「その起点からの距離」の条件付き検証である。
GPU一次レイ生成そのものの独立認定ではない。
float32モデルもGPUのFMAやnormalizeをbit再現するものではない。
これらを理由にdouble oracleの基準を緩めたり、RR native入力の認定へ昇格したりしない。

検証結果:

- fresh captureのD3D12 ERROR/CORRUPTIONは0。
- queryTへ0.01を加えた異常、sampleStartIndex不一致、primary validity=0の3ケースを拒否した。
- renderer / shader / NRD / Hybrid RRは変更せず、再buildも不要だった。
- `git diff --check` は合格。

reports:

- `build/reconstruction-input-validation/captures/rr-distance-precision-20261024/report.json`
- `build/reconstruction-input-validation/captures/rr-distance-precision-20261024/negative-checks.json`

再実行例（sourceとexeのhashが一致していることが必要）:

```powershell
python -B Tests/PathTracing/validate_rr_distance_precision.py `
  --source build/reconstruction-input-validation/captures/rr-distance-lineage-20261024 `
  --output build/reconstruction-input-validation/captures/rr-distance-precision-repeat `
  --exe build/reconstruction-input-validation/Debug/RtPbrSurvey.exe
```

次は実continuationの3信号を共通入力のoptional capabilityに追加する。
NRDの独立probeとの意味の違いを保持し、既存のRR数値診断t7を自動置換しない。
背景 / miss / untracedのRR契約と、native adapterの接続は引き続き未認定である。

Status: Primary precision explains observed distance discrepancy; conditional continuation-distance checks pass, native RR qualification and NRD strict replay investigation remain.

## PTからnative RRへの接続: 60分の実装プラン

目標は、小さい静止fixtureでPT由来の入力をnative RRへ渡し、SDK evaluateの成功と
GPU完了後のRR出力captureを確認することである。
一般シーンの画質完成ではなく、実呼出しが成立する最小経路を作る。
現在の検証exeはStreamline SDK未連携のため、冒頭でSDK有効buildを開始する。
実施workspaceはWork4のみとし、既存の未コミット変更を引き継ぐ。

| 経過 | 作業 | その時点の確認事項 |
| --- | --- | --- |
| 0–8分 | Work4内にRR接続用buildを作り、既存Streamline 2.12.0のinclude / lib / runtime DLLを指定。feature supportと初期化ログを確認。小解像度の閉じたopaque静止fixtureを用意 | SDK未連携ではなく実feature supportの結果が得られる。SDKはWork3にあるものをread-only参照し、build出力はWork4へ置く |
| 8–20分 | `PrimaryContinuation` / `SecondaryRayData` / `SecondarySurfaceDistance` を共通入力へoptional追加。既存signal番号を維持。PT用RR距離候補を実continuationへ接続 | 1spf / pixel-center / 2bounce / BSDF-only環境で同一productionの信号が揃う。独立NRD probeの変換・意味は維持する |
| 20–37分 | PT用evaluate入力をadapterへ追加し、LinearDepth tag、独立したoutput resource、行列、MV scale、exposure、resource stateを接続。renderとoutputは等倍に限定 | Hybridの既存HardwareDepth経路を保持。PTでreflection専用resourceを偽装することなくnative evaluateを記録できる |
| 37–47分 | native opt-inの診断呼出し、初回reset、同一production保持、失敗時のraw PT fallback、結果ログとRR出力captureを接続 | 成功時だけRR出力を使用。SDK失敗 / 入力除外 / 未実行を別々に記録し、raw PTをRR成功として扱わない |
| 47–60分 | Debug build、限定fixtureで1回以上のnative evaluateとcapture。opt-out / gate拒否 / resetの最小確認。既存Hybridの関連契約テストを実行し、結果を記録 | evaluate結果、実行回数、入力frame、output由来、D3D12 ERROR/CORRUPTION、出力finite検査を確認。時間切れ時は実際の到達点を記録 |

初期profile:

- 静止camera / 静止opaque geometry / 固定material / 1spf / pixel-center / jitterなし。
- render=outputの等倍、小解像度（例256x144）、2bounce、direct / emissive / NEE無効。
- 閉じたfixtureでprimary backgroundとcontinuation missを避ける。
  全画素の実continuationがfinite hitであることを検証し、untraced / invalidもnativeへ送らない。
  これはfixtureの条件であり、一般シーンのmiss sentinelを定義するものではない。
- noisy colorにはcurrent-frame radianceを使い、PTの蓄積結果やNRD結果を渡さない。
- albedo / normal / roughness / LinearDepth / MVは既存のRR変換を再利用し、
  specular hit distanceのみ実continuationのsurface-origin幾何距離から生成する。

最初の呼出しは診断用の二段階でよい。
同一productionの変換textureを保持し、GPU完了後に全画素gateを確認してから、
その保持入力に対してRRを実行する。最新frameへstampを付け替えない。
通常の毎frame処理へCPU readback待機を導入せず、この段階では単発接続の証明を優先する。
初回はreset=trueとし、native evaluateが成功した入力だけを履歴基準にする。
pause / 再生成 / resize等で入力同一性が失われた場合は拒否またはresetする。

60分終了時の成功条件:

1. PT由来の検証済み入力に対して、実際のnative RR evaluateが少なくとも1回成功する。
2. SDK出力resourceをGPU完了後にcaptureし、raw fallbackと区別できる証跡がある。
3. D3D12 ERROR/CORRUPTION 0、出力finite、入力とoutputの寸法・productionが一致する。
4. native opt-inを外した場合と入力gate失敗時にはSDKを呼ばない。

SDK / driver / deviceがfeatureを提供しない場合は、8分時点で原因と失敗stageを確定する。
入力gateが通らない場合もgateを緩めず、接続コードと失敗証跡までを到達点とする。
どちらの場合も「native接続成功」とは報告しない。
この60分では背景 / miss契約、動的物体、upscaling、長期history画質、NRD strict replay問題は扱わない。
commit / push / PR mergeはこの時間枠に含めない。

Status: 60-minute native RR connection plan recorded; implementation not started for this plan.

## 60分枠の実装結果: PT入力からnative RRへの単発接続

PT captureからのGPU入力変換、全画素の検証、native RR evaluate、
GPU完了後のSDK出力readbackまで接続し、実機で成功した。
通常描画のPT final colorへの差し替えや連続frameのhistory運用はまだ行っていない。
今回の到達点は、同一productionを保持した単発の診断replayである。

実装:

- 共通入力へ `PrimaryContinuation` / `SecondaryRayData` / `SecondarySurfaceDistance` をoptional追加。
  既存15信号の番号とNRD probeの意味は維持する。
  この3信号は独立NRD distance diagnosticsをOFFにしても取得できる。
- `RayReconstructionEvaluateInputs::pathTracingDiagnostic` を追加（default false）。
  PT診断は独立 `scalingOutputColor` とLinearDepth tagを使用する。
  Hybridは従来のreflection resource / HardwareDepth tag / output stateを保持する。
- PT診断のcolor / albedo / specular albedo / normalはRGBA32_FLOAT、roughness / depth / distanceは
  R32_FLOAT、MVはRG32_FLOAT。render=output、HDR、preExposure=exposureScale=1。
- SDK直前の `slEvaluateFeature(kFeatureDLSS_RR)` の実呼出し回数をdiagnosticsへ記録する。
  support確認やreadiness判定だけではカウントしない。
- `RtPbrSurvey.RrNativeReplay` は既存texture replay harnessのGPU変換を再利用する。
  変換完了fence後にstatus全画素0とprofile / native opt-inを確認し、
  同じtexture allocationをRRへ渡す。NGX出力は別UAVにし、fallbackをSDK成功として使わない。
- 出力をNaNで初期化してからRRを実行し、完了fence後に全RGBが有限になったことを確認する。
  alphaの意味はこの診断で認定しない。
- `Tests/PathTracing/validate_rr_native.py` はfixture生成、10信号のcapture、
  production cohort一致、CPU double参照とGPU変換の比較、native output、拒否ケースをまとめて検証する。
- `-WindowSize width height` を追加。整数64..4096、未指定時は従来のwindow sizeを保持する。
  samplingやdebug layerの設定は変更しない。

限定fixture:

- 256x144、静止camera / geometry、opaqueの閉じたglTF box、1spf、pixel-center、2bounce。
- 正面の金属面を反射した実continuationが発光面へ届く。
  emissiveは有効、direct / environment / emissive NEE / Russian rouletteは無効。
  非黒の入力を得るため、プランのemissive OFFを「BSDF経路で到達する発光のみ」に変更した。
- glTFへ白の定数metallic-roughness / emissive textureを明示する。
  renderer側の欠落texture fallbackへ依存せず、試験用の金属率と発光を再現するためである。
- 実continuationのfinite hitは36,864 / 36,864 pixel。
  背景 / miss / untracedの値を補完してgateを通す処理はない。
- 静止cameraのcurrent / previous matrix一致を確認する。
  world-hitの再投影に由来する微小MVは2e-6 NDC以内を要求し、その値を保持する。
  計測された最大絶対値は約7.15e-7で、0に書き換えない。

検証結果:

- NVIDIA GeForce RTX 2080 Ti、Streamline 2.12.0の既存配置をread-only参照。
  support queryは `Available / Result::eOk`。
- 初回native evaluateは `Result::eOk`、`inputReady=true`、`outputAvailable=true`。
  最終replayでも実呼出し回数は1、reset=true、frameId=31、sampleStartIndex=30。
- GPU完了、全36,864 pixelのRGB有限、D3D12 ERROR/CORRUPTION 0。
  SDK出力RGB範囲は `[0.071868896484375, 0.735595703125]`。
  入力とのRGB差は全pixelにあり、最大絶対差0.003078937530517578。
  これはpassthroughでないことの確認であり、画質改善の認定ではない。
- opt-in OFFと、1 pixelの距離を負にした2ケースはexit 1で拒否。
  両方ともnative evaluate呼出し0、native出力fileなし。
- 共通metadata / allocation identity / NRD capability / RR capability /
  HybridとPTのadapter境界 / CaptureSession CLIの関連CTestは6 / 6合格。
- 最終native replay / 関連testのDebug buildはwarning / error 0。
  `git diff --check` は合格。commit / push / mergeは行っていない。
- SDK有効の最終アプリもcompile / linkでwarning / error 0。
  配置済みruntimeを使い、使用中DLLの再コピーを避けて検証した。
  `-WindowSize 256 144` のPT captureはexit 0、全値finite、D3D12 ERROR/CORRUPTION 0で、
  SDKなしproducerのcurrent-frame radianceとbit一致した。
  このPT起動経路のRR supportログはDeviceNotSetであり、アプリからnative RRを呼んだ検証ではない。
  native RRのAvailable / evaluate成功は前述の専用replay exeで確認している。

reports:

- `build/pt-native-rr-validation/captures/native-rr-20261024-white/report.json`（fresh 10 captureと初回native成功）
- `build/pt-native-rr-validation/captures/native-rr-20261024-final/report.json`（同一captureを再利用した最終native / 拒否確認）
- `build/pt-native-rr-validation/captures/native-rr-20261024-final/gpu/native-result.json`
- `build/pt-native-rr-validation/captures/native-rr-20261024-final/native-off/gpu/admission-result.json`
- `build/pt-native-rr-validation/captures/native-rr-20261024-final/invalid-distance/gpu/admission-result.json`
- `build/pt-native-rr-validation/captures/sdk-app-final-smoke-20261024/report.json`

最終replay exe SHA256は `c3f3c3f8707ec6025499d54e74da05e94b8fc2f67e8db855ea740e6424723f33`。
producer exeは `c61aab59ce8332ac029538d77b75a77f0dc1982fe2456d56dcfc72053d85a026`。
native output SHA256は `253b3adcef7f0e0035daac9a32dac474c9abd69eb39af745208bddf45f7da18d`。
raw reportにはproducer / converter shader、fixture、scriptと各captureのhashも保存する。

再実行は、SDK有効のtest buildと `-WindowSize` 対応producerを使う。
SDK model cache等の利用権限が必要な環境では通常の許可された実行環境で行う。

```powershell
python -B Tests/PathTracing/validate_rr_native.py `
  --source build/reconstruction-input-validation/captures/rr-live-conversion-20261024 `
  --output build/pt-native-rr-validation/captures/native-rr-repeat `
  --exe build/rr-live-only-validation/Debug/RtPbrSurvey.exe `
  --replay-exe build/pt-native-rr-validation/Debug/RtPbrSurvey.RrNativeReplay.exe `
  --shader build/pt-native-rr-validation/Tests/shaders_PathTracingRrConversion_CSMain.cso
```

生成したscene / preset / glTFがhash一致する場合は、`--reuse-captures <capture-folder>` で
同じPT入力のままnative呼出しと拒否確認を再実行できる。
native replay用exeはSDKのruntime DLLを `copy_if_different` で配置し、不要なAssetsや
使用中のAgility DLLを再コピーしない。

初期検証で残した失敗資料:

- `closed-box-20261024`: 未対応のscene JSON material項目を含むため起動できなかったfixture。
- `native-rr-20261024/report.json`: SDK有効producerがcapture後に非0で終了した試行。
  詳細原因は未調査。producerをSDKなしのbuildへ分離し、native replayはSDK有効で成功を確認した。
  最終アプリの小解像度smokeでは正常終了したが、初期試行の原因を修正したとは断定しない。
- `native-rr-20261024-small/report.json`: 静止MVのbit 0要求で失敗した試行。
  camera matrix一致と前述のreprojection noise条件へ分離した。

次は通常のPT render graphへのnative evaluate配置、出力選択、history reset / 継続処理を行う。
背景 / miss / untraced契約、動的物体、upscaling、画質比較とNRD strict replay問題は未解決である。

Status: Native RR single-production replay validated, including output and negative admission; real-time PT integration and general-scene qualification remain.

## 次段階: RR履歴と出力選択の状態管理

共通入力 / NRD境界 / RR変換 / native診断までを `03d3ba4`
(`Add reconstruction input boundaries and native RR diagnostic replay`) にコミットした。
生成ログ / captures / build outputsは含めていない。

続いて `Renderer/PathTracingRrHistory.h/.cpp` を追加した。
通常PT render graphへ配置する前に、評価と表示の状態を次のように定義する。

- 入力は、検証済みかつ変更されないproduction packetに限定する。
  この部品はpixel / SDK / distance lineageの認定を代行しない。
- keyにはproducer frame、sample start、sampling signature、reset token、render/output寸法、
  current / previousの正確なview-projectionを保持する。
  初期profileは引き続き1spf。reset tokenはgeometry / material / lighting等の編集やcamera cutで変更する。
- 初回はreset=true。同じproductionと出力allocationの再表示は、SDKを呼ばずにreuseする。
- 連続frame / sampleで、今回のprevious matrixが最後に成功したcurrent matrixと一致する場合のみ、
  reset=falseとする。通常のcamera移動でcurrent matrixが変化すること自体は拒否しない。
- 寸法 / sampling / reset tokenの変更、frame / sampleの欠落、camera履歴不一致ではresetする。
  stale / 順序逆転 / 不正keyはPT fallbackとし、古いproductionを新しい履歴へ付け替えない。
- submissionごとのticketを使う。pending中に別packetを差し替えず、古い完了通知は無視する。
  disable / 入力拒否はpendingも無効化し、後から届いた成功でRR出力が復活しないようにする。
- SDKが成功しただけでは、出力を表示可能にせず、履歴基準も更新しない。
  GPU完了と出力検証が済んだ時だけ、成功keyと出力allocationをcommitする。
  SDK失敗、明示invalidate後の次の評価はreset=true。
- allocationとGPU fenceの所有はcallerに残す。pending / 表示可能な間はallocationを保持する。
  GPU fence失敗や不正pixelを検出した場合は明示Invalidateする。
  この部品のInvalidateはGPU処理のcancelやresource解放を行わない。

native replayにも同じ管理を接続した。
実際のSDK成功後からGPU完了前の段階では `historyPendingGpu=true` かつ表示不可を確認し、
fence / finite RGB / D3D12チェック後に `historyCommitted=true`、`rrOutputSelected=true` とする。
このreplayのreset tokenは、hash一致を確認した固定fixtureの1であり、通常描画用のscene revisionではない。

検証:

- Debug build成功、warning / error 0。
- 関連CTest 7 / 7合格。新しいhistory testは、成功commit、pending、pause reuse、
  camera移動、各reset条件、SDK失敗、stale completion、disable後の遅延完了、
  allocation不一致、NaN / singular camera、sample / producer sequence境界を確認した。
- 同じPT capture cohortを再利用した実機native replayも成功。
  native evaluate 1回、`Result::eOk`、GPU完了、RGB有限、D3D12 ERROR/CORRUPTION 0。
  GPU完了前はpending、完了後はcommit / RR出力選択を確認した。
- opt-in OFF / 不正距離は引き続きnative呼出し0で拒否した。
- native output SHA256は前段と同じ
  `253b3adcef7f0e0035daac9a32dac474c9abd69eb39af745208bddf45f7da18d`。

report:
`build/pt-native-rr-validation/captures/rr-history-20261024/report.json`

build log:
`C:/work/RtPbrSurvey-agents/pt-rr-history-work4`

次は、通常PTのframeごとに入力を保持するqueueとGPU検証の完了通知をこの管理へ渡し、
render graphのnative evaluateと最終出力選択へ接続する。
連続する実GPU frameのRR history、通常描画への差し替え、背景 / missはまだ検証していない。
今回追加したhistory管理は未コミットである。

Status: Previous diagnostic milestone committed; RR history/output state management implemented and validated in unit tests and native replay, real-time integration remains.

## 2026-10-24: Backend切替と実行性能の計測基盤

目標は、通常PTでNative / NRD / RRを切り替え、各backendを個別に最適化できる構成とする。
共通入力はresource参照とframe情報の契約に留め、未選択backendのための変換を常時実行しない。

今回の実装:

- `pathTracing.reconstructionBackend` は `native` / `nrd` / `rr`。UI、preset保存、
  CLI `-PathTracingReconstruction native|nrd|rr` から選択する。
  既存presetの `nrdPreview` は新しい選択が未指定の場合に互換処理する。
- 切替時はPT/NRD履歴と表示出力を無効化する。
- NRD選択時はNRD変換、hit distance再構成/admission、RELAX、合成を実行する。
  RR選択時はRR変換を実行する。Native選択時はどちらも実行しない。
- 未選択側の通常frameの出力clearとそのresource宣言を省く。
  履歴reset時の初期化、共通guide生成、確保済みVRAMは従来どおり。
- inspectorでNRD/RR resourceを観察する場合と、明示的な
  `reconstructionDiagnostics=true` は対象の診断処理を実行する。
  そのframeは `reconstructionDiagnosticWork=true` とし、通常性能と区別する。
- 既存のGPU timestampを再利用し、NRD変換、再構成/admission、SDK、合成、
  RR変換、RR SDKを分けてUIとcapture logに記録する。追加の同期readbackは導入しない。
  pass境界のresource transition時間を含む。
- timestampを回収した同じframe resourceからbackendも回収する。
  切替直後に旧frameの時間を新backendの時間として表示しない。
- 未実行stageはJSON null / UI未実行表示とする。未接続のRR SDKを0msとは扱わない。

RRの通常描画へのnative evaluate / 最終出力接続は未実装である。
RR選択は現時点では入力変換までで、表示はNative PTへfallbackし、UIにも未接続を表示する。
前節のnative replayによるSDK実行・GPU完了・出力検証とは明確に区別する。

性能改善の順序:

1. 同一scene、解像度、1 spp、seed、camera、warm-up条件で各stageを計測する。
2. RRを通常render graphへ接続し、GPU fenceと履歴/output管理を統合する。
3. RRは実際のPT continuationから距離を取得し、追加probe ray依存を外す。
4. 数値・画質比較を維持しながら、互換resourceの直接参照、不要診断出力の除去、
   必要箇所のFP16化、変換pass統合を検討する。
5. NRD/RRそれぞれのSDK時間とPT+変換+SDK+合成の合計を評価する。
   現在のRGBA32診断出力と小さいfixtureの単発測定だけで実用性能を結論しない。
検証結果（RTX 2080 Ti、Debug / D3D12 Debug Layer ON、256x144、closed-box、1 spp、
2 bounce、pixel-center、frame 60後の単発timestamp。平均/実用benchmarkではない）:

| 選択 | NRD変換 | NRD再構成/admission | RELAX | NRD合成 | RR変換 | RR SDK |
|---|---:|---:|---:|---:|---:|---|
| Native | 未実行 | 未実行 | 未実行 | 未実行 | 未実行 | 未実行 |
| NRD | 0.031456 ms | 0.028800 ms | 0.554432 ms | 0.012768 ms | 未実行 | 未実行 |
| RR | 未実行 | 未実行 | 未実行 | 未実行 | 0.016384 ms | 未接続 |

3モードの実プロセス終了code 0、D3D12 ERROR 0。
NRD表示はRELAX diagnostic（pixel単位Native fallbackあり）、RR表示はNative PT。
入力CurrentFrameRadianceはsampleStartIndex 60 / productionFrameId 61で3モードbit-exact。
データ部SHA256: `1525dbd457fd48e7ec9ed1306c0bc3a7991f9f6203bce7f8c5d8c420812920bc`。
report: `build/reconstruction-input-validation/captures/backend-selection-20261024/report.json`。

CPU検証: backend選択、履歴管理、settings互換/保存、CLI/CaptureSessionを確認。
RenderPresetStoreは既定TEMPでatomic saveが失敗したため、workspace内の専用TEMPへ切り替えて再実行し成功。
これは初回失敗を隠さず記録し、製品側のatomic save処理は変更していない。

切替と選択側だけの実行・計測の基盤は追加済み。
RRの通常PT出力接続、および両backendの本番解像度での性能最適化は継続課題である。
最終Debug buildはerror 0（test executableの既存LNK4199 warning 1）。上記CPUテスト5件は専用TEMPで全件成功。
Native選択中のNRD inspectorも実機確認し、NRDのみ診断実行 / RR未実行 / diagnosticWork=trueとなった。
数値比較用のNRD/RR capture scriptは、raw入力と変換後を同じ診断cohortで取得するため、明示的にreconstructionDiagnosticsを有効にする。通常描画の既定値はfalse。

## 2026-10-24 Step 1: NRD/RR共通管理コードの整理

目的は、通常描画へのRR接続に先立ち、共通の契約・管理とSDK固有の処理を分けること。
共通のSDK基底classや統一packing形式を新設するのではなく、既存adapterの呼び出し点へ共通関数を適用する。

共通化した処理:

| 対象 | 共通コード | 利用箇所 |
|---|---|---|
| 入力の必須signal検査 | `FindMissingPathTracingSignal` | NRD/RR metadata検証。各backendはsignal一覧と固有issueを保持 |
| カメラ行列検査 | `IsPathTracingCameraMatrixUsable` | 共通入力検証、RR履歴key検証。有限/非特異の同じ基準 |
| production識別 | `PathTracingFrameStamp` | frame/seed-profile/sample/解像度をRRのGPU結果と照合 |
| カメラ・解像度の取得元 | `PathTracingReconstructionProduction::Inputs().frame` | NRD SDK frameもPT記録時の情報から設定 |
| 履歴reset要求 | `PathTracingReconstructionHistoryState` | NRDの独立reset boolを置換。NRD/RRごとにreset要求とepochを保持 |
| 表示可否・fallback | `ResolvePathTracingReconstructionPresentation` | 実際のtone-map入力選択、UI、capture log、既存NRD API互換wrapper |
| GPU計測項目一覧 | `kPathTracingReconstructionStages` | UI表示名とJSON keyの一覧を共有。既存timestamp集計を継続 |

表示条件の優先順位は、Native選択、未build、backend利用不可、入力不適合、出力待ち、利用可能。
NRD/RR両方に同じ判定を適用し、requested backendと実際のoutput backendを区別する。
未接続のlive RRは `backend-unavailable` / output `native` とする。
旧NRD presentation文字列・enumは互換wrapperで従来の意味を保つ。

履歴resetは、scene/settings/backend切替などで両backendへ要求する。
NRD固有の連続camera履歴を保持する場合はNRDのみ保持し、RR側のepochは更新する。
NRD固有のcamera不整合やSDK失敗はNRDだけをresetする。
reset要求のacknowledgeは、adapterがresetを受理したことを表し、GPU完了や出力表示許可ではない。
RRのpending ticket / GPU verification / allocation照合は既存のRR履歴管理に残す。

共通入力resourceは従来どおり参照する。画像コピー、追加のGPU pass、同期readbackは追加しない。
NRDのdemodulation / reconstruction / packing、RRの入力形式・pixel admission・SDK呼び出しは変更しない。
共通の露出契約（scene-referred / preExposure=exposureScale=1）も維持する。
Step 1検証結果:

- NRD有効 / RR変換有効のDebugアプリと関連testをbuild: error 0。
  NRD adapter testの既存LNK4199 warning 1。build log: `C:/work/RtPbrSurvey-agents/pt-common-step1-work4`。
- RR SDK有効 / NRD無効のnative replayをbuild: warning 0 / error 0。
  build log: `C:/work/RtPbrSurvey-agents/pt-common-step1-rr-work4`。
- 共通入力、NRD入力、RR入力、共通policy、RR履歴、NRD adapter、settings、CaptureSessionのCTest 8/8成功。
- 256x144 / 1 spp / 同一sceneでNative・NRD・RRの選択側だけの実行を再確認。
  全プロセスcode 0、D3D12 ERROR 0。追加したactual output / fallback reasonも期待どおり。
  3モードのCurrentFrameRadianceは変更前とbit-exact。NRD last evaluationのcamera/frame metadataも完全一致。
  report: `build/reconstruction-input-validation/captures/backend-common-step1-20261024/report.json`。
- RR native診断replayは、hash照合済みの既存入力を再利用し、新しいbinaryで実際にSDK evaluateを実行。
  `Result::eOk` / native call 1 / GPU完了・RGB有限 / history commit成功 / D3D12 ERROR 0。
  native opt-in OFF / 不正distanceの2件はSDK call 0で拒否。
  出力SHA256は変更前と同じ `253b3adcef7f0e0035daac9a32dac474c9abd69eb39af745208bddf45f7da18d`。
  report: `build/pt-native-rr-validation/captures/rr-common-step1-20261024/report.json`。

単発timestampの変動を性能改善とは解釈しない。Step 1ではshaderとSDK設定の最適化は行っていない。
共通管理コードの整理は完了。Step 2のRR通常描画接続は未着手で、変更は未コミット。
## 2026-10-25 Step 2: 保持したPT frameのnative RR通常表示

最初のlive接続は、検証済みの静止1 frameを保持する診断profileとする。
通常render graph内でRRを実行してtone-mapへ渡すが、連続frame denoisingではない。
入力保持中と表示中はPTの新規dispatchを止める。camera/scene/settings/backendのresetで再取得する。

実装:

- `PathTracingRrLive` が入力admission待ち、SDK実行待ち、出力検証待ち、表示可能、拒否の状態を管理する。
  8つの変換済み入力と専用RGBA32出力をComPtrで保持し、allocationとframe keyを照合する。
- 実際のPT continuationから `SecondarySurfaceDistance.x / SecondaryRayData.z / SecondaryRayData.x` を取得する。
  live shaderのt8/t9とmetadataの必須入力一覧を共通定義し、t0..t7の診断replay契約は維持する。
- live profileは静止camera、pixel-center、1 spp、非accumulation、2 bounce、距離診断ON、Radiance view。
  全画素でmetallic=1、roughness=0、実際のspecular continuation、有限traced-hit距離が必要。
  motionの浮動小数誤差は既存replayと同じNDC絶対値2e-6以内。
  背景、miss、非鏡面/非金属、未trace、非有限入力はnative SDKの前で拒否する。
- 入力statusのGPU全画面集計を8 byteで回収する（拒否bit、訪問pixel数）。完了fenceを確認してからSDKを呼ぶ。
- SDK出力はRGBをNaNで初期化し、実行後に別のGPU全画面集計でRGB有限・全pixel coverageを確認する。
  出力検証も8 byteのみ回収し、完了後にRR履歴管理へcommitする。
- fenceは通常frameのsubmissionへ紐付け、`CompletedFenceValue` を非同期pollする。
  この経路からCPUのGPU強制待ちや画像readback/copyは追加しない。
  resetでキャンセルしても、in-flightの集計/保持resourceは完了まで再利用しない。
- 共通presentationがRR Readyを返した場合に、tone-mapの入力SRVとresource read宣言を専用RR出力へ切り替える。
  待機・拒否・native OFF時はNative PTを表示する。
- `-PathTracingReconstruction rr` と既存native opt-inフラグの組合せだけを許可する。
  PTとDLSS SR / Hybrid reflection captureの排他は維持する。
- PT起動時のStreamline初期化とdevice設定はnative opt-in時に有効にする。

実行例（SDK有効・RR変換有効のbuild）:

```powershell
.\build\pt-native-rr-validation\Debug\RtPbrSurvey.exe `
  -SceneFile <closed-box-scene.json> -RenderPreset <validated-preset.json> `
  -WindowSize 256 144 -PathTracingReconstruction rr `
  -EnableDlssRayReconstruction -EnableExperimentalNativeRayReconstruction `
  -CapturePath <viewport.png> -CaptureAfterFrames 60 -ExitAfterCapture -LogToFile <runtime.log>
```

診断logには要求backend、実output backend、fallback理由に加え、`ptRr.phase`、
`retainedFrameId`、`outputVerified`、native SDK call数を記録する。
`PT DLSS RR Native` のtimestampは出力初期化・SDK・GPU検証を含むpass全体であり、SDK単独benchmarkではない。
保持後のframeではSDKを再実行しないため、そのframeのSDK時間は未実行となる。
追加GPU集計とRGBA32出力のコストを含めた性能比較はStep 5で行う。

連続更新、移動cameraのRR履歴、一般material/background/missへの対応はStep 3以降に残す。
Step 2検証結果（RTX 2080 Ti、256x144、Debug / D3D12 Debug Layer ON）:

| case | SDK evaluate回数 | 最終表示 | GPU出力検証 | D3D12 ERROR |
|---|---:|---|---|---:|
| RR output texture | 1 | RR | 完了・全RGB有限 | 0 |
| RR通常viewport | 1 | RR | 完了 | 0 |
| native opt-in OFF | 0 | Native | 未実行 | 0 |
| 背景を含む入力 | 0 | Native | 入力admissionで拒否 | 0 |
| 非鏡面material | 0 | Native | 入力admissionで拒否 | 0 |

成功2件のretainedFrameIdは1、phaseは`retained-output-ready`、`outputVerified=true`。
SDK結果は`Result::eOk`。通常viewportのPNGも確認済み。
`Tests/PathTracing/validate_rr_live_native.py` で5件を再現できる。
report: `build/pt-native-rr-validation/captures/rr-live-step2-restored-20261025/report.json`。

CPU検証はNRD構成9/9、RR SDK構成5/5成功（manifest、CLI、共通policy、入力、履歴など）。
最終SDK build: warning 0 / error 0、log `C:/work/RtPbrSurvey-agents/pt-rr-state-restore-work4`。
NRD構成の最終buildもerror 0、log `C:/work/RtPbrSurvey-agents/pt-rr-step2-final-nrd-work4`。
NRD構成のNative/NRD/RR選択を実機で再確認し、全件process code 0 / D3D12 ERROR 0。
CurrentFrameRadianceは変更前とbit-exact、NRD last evaluationのcamera/frame metadataも一致。
report: `build/reconstruction-input-validation/captures/backend-step2-regression-20261025/report.json`。
単発timestampの変動は速度改善として評価しない。

実装中に検出・修正した点:

- 既存CLIのPT/DLSS排他判定がnative RR指定も拒否していた。RR選択時の許可を追加し、CLIテストを追加。
- 専用RR出力の初回clearにresource write宣言が不足し、未確保resourceを参照していた。graph宣言を修正。
- 入力admissionがfence未接続で待機したままになった。通常frame submissionとRenderFrame冒頭のpollへ接続。
- SDK/validation後に古いgraphics/compute descriptor状態が残り、Draw時にD3D12のdata-static違反が出た。
  SDK前後でroot bindingを解除・復元し、上記5件でERROR 0を確認。

初回失敗reportやlogはbuild配下に保持し、成功扱いにはしていない。
Step 2の「検証済みの保持frameを通常render graphからRR表示する」接続は完了。
連続frame / 移動camera / リサイズと切替の実機検証はStep 3。変更は未コミット。
## 2026-10-25 Step 3: 連続productionと切替・リサイズ検証

Step 1/2をcommit `9a299e3` として保存した後の作業。
静止cameraの検証済みprofileに、`rrContinuous`（既定false）とCLI `-PathTracingRrContinuous` を追加する。
同じprofileの保持frameモードは継続して利用できる。

- 同時に扱う未完了productionは1つとし、入力admission、SDK、GPU出力検証の所有関係を維持する。
- RR出力を2枚に分ける。検証済みのpublished出力を表示しながら、もう片方へ次の結果を生成する。
  画像copyは追加せず、GPU検証完了時に表示先を切り替える。published allocationへの上書きは拒否する。
- 非accumulation時の通常clearから両出力を除外し、SDKの書込先だけを評価前に初期化する。
  scene/settings/backend/camera/resizeのreset時はpublished出力を無効化する。
- 連続するPT productionではframeId/sample indexが1ずつ進み、前回camera行列が一致する場合にSDK reset=falseとする。
  display frameとPT productionの進行は別であり、この安全なsingle-flight経路の更新頻度はdisplay FPSと同じではない。
- published frameIdと処理中input frameIdを区別し、完了数、連続評価数、reset数、cancel数を診断へ追加する。
- pause後は新規productionを開始しない。処理中の仕事はfence完了まで保持する。
- camera移動中は従来の静止条件を外れるためNativeへfallbackし、停止後の入力をreset=trueで再取得する。
  移動中のRR履歴継続や一般materialへの対応は、この変更では追加しない。

追加のRGBA32出力1枚は16 byte/pixel（1920x1080で約33 MB）のVRAMを使う。
強制GPU待ちと画像readback/copyを避けるための保持領域であり、性能最適化済みとは扱わない。

`-PathTracingRrLifecycleValidation <output-directory>` はSceneFile / LogToFile / native opt-inを必須とする検証経路。
同一プロセス内で、連続RR、pause/resume、Native→NRD→RR、入力admission待ちのreset、
camera移動後の復帰、320x180へのresizeと256x144への復帰を確認し、最終viewportを保存する。
NRD/RRを同時に組み込んだ構成で `Tests/PathTracing/validate_rr_lifecycle.py` から実行する。
Step 3検証結果:

- NRD 4.17.3 と Streamline 2.12.0 の両SDKを有効にしたDebug buildが成功（error 0、CPU testの既存LNK4199 warning 2）。
  build log: `C:/work/RtPbrSurvey-agents/pt-rr-continuous-sdk-work4`。
- 関連CTestは9/9成功。CLIの連続RR指定、設定保存も検証した。
- 同一GPUプロセスで10 checkpointが成功、process code 0、D3D12 ERROR 0。
  初期連続評価は完了3回 / reset 1回 / continuous 2回。
  pause後もSDK call数とpublished frameIdは不変。resume後はresetを増やさずcontinuousが4回へ進んだ。
- Native選択時はNative、NRD選択時は実際のRELAX出力となり、その間RR SDK call数は不変。
  RRへ戻すとreset=trueで再取得した。
- admission待ちresetでは、キャンセルした入力がSDKへ進まず、新しいproductionで再開した。
  camera移動中も古いRR出力を表示せず、停止後に再開した。
- 320x180と256x144の両resize後に、新しいallocationで検証済みRR出力へ復帰した。
  checkpoint上で完了10回、reset 6回、continuous 4回、cancel 4回を確認。
- 最終viewport PNG（256x144）を保存した。
  report: `build/reconstruction-input-validation/captures/rr-lifecycle-20261025/report.json`。
  executable SHA256: `d11b02fccafae6c15356db724d5e9cf04e1bb71841faec4fe6094a89cca3b47d`。
- 既存の保持frameモードの5ケース（RR output / viewport / opt-in OFF / background / 非鏡面）も全件成功、D3D12 ERROR 0。
  report: `build/reconstruction-input-validation/captures/rr-retained-regression-step3-20261025/report.json`。

この段階で、検証済み静止profileの連続更新、停止再開、backend切替、cancel、camera停止後の復帰、resizeを確認した。
移動中のRR履歴継続、一般material/background/missのRR処理、性能最適化は未完了。
Step 3の変更は、Step 1/2のcommit `9a299e3` に続く未コミット変更である。