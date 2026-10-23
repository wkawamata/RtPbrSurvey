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

Status: first implementation unit validated; producer binding and backend conversions remain pending.
