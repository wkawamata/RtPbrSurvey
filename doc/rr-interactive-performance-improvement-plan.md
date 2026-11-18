# RR実装の問題分析と改善方針

作成日: 2026-11-19
対象: Work4、codex/rr-eager-production、PR #132 の現行PT/RR経路。
これは実装計画であり、以下の改善をすべて実装・検証済みという意味ではない。

## 結論

RRの接続・履歴・不正入力拒否は自動検証されている。一方、カメラ操作時に
新しい画像が十分な頻度で届くという実用性能は未達である。
表示回数を増やすより、検証済みの新しいRR画像の更新頻度と、その画像が表す
カメラ姿勢の古さを改善する。非ブロッキング取得は入力サービスの実験として
分離し、描画高速化の成果として扱わない。既定OFFを維持する。

## 確認できた問題

1. WorkMeter Averageはフレーム毎のFPSを算術平均している。
   CPU処理時間の逆数も表示しており、非ブロッキングONでは外側の待機時間が
   除外される。VSync ONでも高いCPU FPSが出る。これは表示頻度ではない。
2. Averageは最新GPU読み戻しをUIフレーム毎に集計する。GPU packet IDで
   重複を除外せず、GPU[index]名で集計するため、同じパスが複数行になる。
   CPU/UIフレーム、GPU submission、RR productionが混在した平均では
   クリティカルパスと実行回数を確定できない。
3. CPUの公開Pollは未完了なら戻る。通常フレーム末尾の資源再利用フェンス待ちは
   別の待機である。非ブロッキング取得は後者を外側のメッセージループへ移す。
   GPUのPT/RR時間や単一production制限は変わらない。
4. 現行PTはResolveRenderDimensionsでrender=outputを強制する。
   ExecutePtRrValidationPassの履歴キーもrender/output同寸であり、
   RR adapterも出力サイズを入力サイズから設定する。少なくともこのPTネイティブ
   経路は等倍であり、HybridやDLSS SRの設定がPTのアップスケールを保証しない。

コード根拠:

- App/DebugUi.cpp: WorkMeterAverage、CPU FPS/Wall FPS、GPU平均の集計。
- Engine/RtPbrSurveyEngine.cpp: ResolveRenderDimensions、ExecutePtRrValidationPass、
  RunFrame、AcquireFrameResources、MoveToNextFrame。
- Renderer/PathTracingRrLive.cpp: Poll、RecordEvaluate。
- Renderer/StreamlineAdapter.cpp: EvaluateStreamlineRayReconstruction。

## 今回のユーザ計測から言えること・未確定のこと

1920x1009、Warm Glass Table、VSync ONのOFF/ON比較では、平均Wall Frameが
37.91/47.64ms。平均間隔の逆数は約26.4/21.0表示回/秒であり、FPS算術平均の
33.78/25.05とは一致しない。120 UIサンプルにPT/RR各30サンプルという記録は、
約4表示フレームにつき1productionの動作と整合する。ただしGPU読み戻しの
重複・遅延があるので、これだけで正確なRR公開頻度は確定しない。

NonblockingのON/OFFと、eager/safe-serial/chainedの選択は独立している。
既定はeager、chainedは別のCLI選択であり、Nonblockingのチェックを入れただけで
chainedにはならない。今回のユーザ実行がchainedだったとは断定しない。

ON時にPT/RRのGPU時間そのものも増えている。これは計測母集団、GPUクロック、
負荷、VSyncによる実行間隔、設定差を含めて再現確認する。CPU待ちの移動だけを
根拠に原因を断定しない。既存の小解像度Release測定とも混ぜない。

## 指標の定義

- 提出頻度: Present呼出数 / 同じ区間の実時間。実モニターに表示された回数や
  input-to-photonとは区別する。VSync OFF時はドロップ等も考慮する。
- Fresh RR/s: 有効な新規RR出力の公開数の差 / 実時間。
- 更新間隔: 新規公開同士の間隔のp50/p95。保持画像の再提示は数えない。
- Production latency: カメラ/物体姿勢のsnapshot取得からGPU完了確認・公開まで。
- Pose age: 現在の画像が表す姿勢の取得時刻から現在までの時間。
  既存retainedAge（公開後の経過時間）とは異なる。
- 入力サービス遅延: Windows入力が処理されるまでの時間。画像へ反映される遅延、
  モニター上のinput-to-photonとは別指標。
- CPU: renderer内訳に加え、host待機・取得時readback・UI/ログを記録する。
  CPU busy cyclesが必要なら別途CPU profilerを用いる。
- GPU: production IDとsubmission IDで対応づけ、PT、変換、検証、RRを
  実行したpacketだけ集計する。非実行はmissingとし、0ms実行に混ぜない。

## 実施順

時間は実装と一回の自動確認を含む概算。環境競合待ち、追加不具合対応、
ユーザ確認待ちは別。各段階を小さいコミットにし、未検証の方式は既定化しない。

| UID | 内容 | 難易度 | 目安 | 達成条件 |
|---|---|---|---|---|
| RRI-01 | 指標修正・実行方式の明示 | 低〜中 | 3〜5時間 | FPSを実時間比へ変更。Fresh RR/s、更新間隔、pose age、現在schedule/解像度/SPP/bounces/VSyncを出力。GPU集計をpacket重複排除・パスIDで統一。CPU区間を整合させる |
| RRI-02 | 実使用条件の再現とタイムライン分析 | 中 | 3〜5時間 | 1920x1009のReleaseでVSync ON/OFFとserial/chainedを比較。NonblockingはまずOFF固定。入力snapshot、記録、submit、GPU完了観測、公開、Presentを同じproductionへ紐付け、待ち時間を説明できる |
| RRI-03 | 既存chainedの実解像度での採否 | 中 | 3〜5時間 | 同一v1入力でsafe-serial対chainedをABBA比較。ガラス・移動・回転で安全性を維持し、Fresh RR/sとpose age/p95が改善する場合だけ推奨候補にする。元のeagerとは別比較であることを明示 |
| RRI-04 | PT/RRの入力・出力解像度分離とアップスケール | 高 | 10〜18時間 | RR出力を表示解像度、PT/guideを適切な低解像度へ分離。対応するSDK設定照会、motion/jitter/extent、履歴キー、検証coverage、出力slot、resize/resetを一括整合。Qualityから性能と画質を評価 |
| RRI-05 | 実測で重いGPUパスを最適化 | 中〜高 | 5〜10時間 | PT/透過bounce/直接光/MIS等を実測で分解。無駄なdispatch・変換・バリア・検証処理を特定し、意味と品質を保って削減。単なるSPP/bounce低下は品質設定として別記 |
| RRI-06 | 公開・入力応答方式の追加設計 | 高 | 設計3〜5時間、実装8〜20時間 | GPU idleやhost遅延が支配的と確認された場合に実施。専用fence eventによる入力併用待機、必要ならGPU検証済みslot選択等を設計。通常tick毎のCPU公開確認の制約を評価し、所有権・拒否・epochの検証を先行する |

RRI-01/02を最優先とする。RRI-02でGPU処理時間が支配的ならRRI-04/05を
優先し、公開/待機の空白が支配的ならRRI-03/06を優先する。全部を直列で
実装する前提ではなく、測定結果で後半の順序と範囲を決める。

RRI-03は安全化済み入力の比較を維持する。生入力を無検証でSDKへ渡す、
不正・未完了出力を公開する、拒否後の履歴resetを省略する変更は行わない。
毎表示tickで同じ古い入力をRR再評価して回数だけ増やす方式も採用しない。

## 性能・品質の採否

暫定目標は、今回の実使用条件に対してFresh RR/sを少なくとも2倍へ近づけ、
カメラ操作時のpose age/p95を短縮すること。これは達成保証ではない。
30以上のFresh RR/sを目指す場合、現行等倍のPT/RRコストとの整合を確認し、
低解像度入力を含めた品質/性能の設計が必要になる。

同一解像度・同一入力のスケジュール変更は既存の完全一致比較を維持する。
アップスケールは画素が異なるため、完全一致を合否条件にしない。高サンプルの
PT参照と既存RRの双方を用い、ガラス上端、厚板の側面、色帯、細線、反射像、
移動・回転・停止後の残像を比較する。画質の主観確認はこの段階で必要となる。

全段階で不正出力公開0、D3D12 ERROR/CORRUPTIONなし、停止/再開、scene reload、
NRD/RR切替、resize、履歴/姿勢対応を保つ。表示FPSだけ上がってFresh RR/sや
pose ageが悪化する変更、待機を計測外へ移しただけの改善は合格にしない。

## まず行う作業

RRI-01でWorkMeterを信頼できる状態にし、RRI-02で今回の1920x1009条件を
再現する。低解像度の既存ベンチマークを繰り返すだけでは、今回の体感問題の
解決確認にはしない。今のNonblockingは既定OFFのまま評価を続ける。

## Review記録（2026-11-19）

判定: 方針は維持する。計測・比較条件を以下のとおり修正してから実装に着手する。
前節までの方針は初版として残し、実施時は本Reviewと末尾の改訂実施順を優先する。
本記録は計画・コードレビューであり、新しいGPU性能測定の結果ではない。

### P1: PresentとRR完了フェンスの関係を先に解析する

現行RenderFrameはPresent、MoveToNextFrame内のSignal、RR MarkSubmittedの順。
表示側の制約がRR完了観測へ影響している可能性を調べる。VSyncが原因であると
現時点で断定はしない。GPU終了、Present前後、Signal、CPUのフェンス完了観測、
公開を分け、GPU時間とCPU時刻の対応も確認する。CPUが完了を観測した時刻を
GPUが実際に処理を終えた時刻と同一視しない。

根拠: Engine/RtPbrSurveyEngine.cppのRenderFrame、MoveToNextFrame、
Renderer/PathTracingRrLive.cppのPoll。フェンスを単純に前へ動かす変更はせず、
production完了とフレーム資源の安全な再利用を区別して設計・検証する。

### P1: RR公開と新しいRR画像の提示を区別する

Fresh RR/sは公開頻度であり、操作に追従した画像がPresentへ渡った頻度を
それだけで保証しない。ToneMapが採用したRR production IDをPresentの
submissionへ紐付け、異なるRR画像を提示した回数と姿勢の古さを記録する。
公開回数が増えても提示画像が古いままなら、操作性の改善と判定しない。
Presentへの提出はモニターでの実表示・input-to-photonとは区別する。

### P1: serial比較と基準設定を明確にする

ユーザ設定のeagerと、v1安全化入力を使うsafe-serialは別条件である。
次の比較順とし、入力プロファイル、スケジュール、ホスト待機の変更を混ぜない。

| 比較 | 確認する影響 |
|---|---|
| ユーザ設定そのままの再現 | 現象と基準値の再現性 |
| eager対safe-serial | 入力プロファイル変更 |
| safe-serial対chained | 同じv1入力でのスケジューリング |
| 同じ方式でNonblocking OFF対ON | ホスト待機方式 |

ビルド種別・commit・SDK/driver、保存シーン/評価設定、実行フラグ、SPP、bounces、
乱数seed、透過実験フラグ、入出力解像度、VSync、カメラ/アニメーション動作、
他GPU負荷とGPUクロック等の状況を保存する。静止と動作中は別集計にする。
1920x1009の実条件を優先し、別解像度・別シーンの過去値で代用しない。

### P2: アップスケール対応を段階分割する

RRI-04は次の三段階に分ける。10〜18時間は暫定見積もりで、境界条件の
棚卸し後に各段階を再見積もりする。

1. 入力/出力解像度の契約と影響範囲を整理し、Qualityの試作を作る。
2. guide、motion/jitter/extent、出力検証coverage、履歴、slot所有権、
   Native/Quality切替、リサイズを検証する。
3. 共通adapterのHybrid回帰と、ガラス・細線・反射・動作時の画質を評価する。

現行のPT等倍固定はコードで確認済み。ただし、アップスケールを最初の改善実装に
決定することとは別であり、RRI-02の結果を受けて優先度を決める。

### P2: 数値による採否条件を定める

「2倍へ近づける」は方向目標として残すが、合否条件としては使わない。
RRI-01/02で基準値を確定後、同一条件・反復測定のばらつきを考慮し、
新しいRR画像の提示頻度、姿勢の古さのp95、許容する画質劣化を定める。
CPU待機を計測外へ移しただけ、または保持画像の再提示回数だけ増やした変更は
合格にしない。安全性の既存合格条件は緩めない。

## Review反映後の実施順

1. RRI-01: FPS計算、区間定義、GPU packet重複排除、実行設定の明示を修正。
   公開IDとPresent採用IDを区別し、提示頻度・姿勢の古さを測れるようにする。
2. RRI-02: ユーザの実使用設定を保存・再現。Present/Signal/完了観測/公開の
   タイムラインを取得し、VSync ON/OFF、GPU負荷・クロックの影響を分離する。
3. 空白・同期が支配的ならRRI-03/06、GPU処理時間が支配的ならRRI-04/05を
   優先する。入力プロファイルの比較を挟み、原因が混ざった比較を避ける。
4. 基準値に対する採否条件を確定し、自動回帰と必要なユーザ画質・操作性確認を行う。

当面Nonblockingは既定OFF。アップスケールは有力候補だが、原因の切り分け前に
先行実装するとまでは決めない。未検証の方式を既定化せず、小さいコミットで進める。
