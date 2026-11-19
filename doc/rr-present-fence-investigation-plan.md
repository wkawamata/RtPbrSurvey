# P1: PresentとRR完了フェンスの関係を調べる作業計画

作成日: 2026-11-19
対象: Work4 / codex/rr-eager-production。改善方針MDのP1調査を先行する。
本書は作業計画。新しいタイムライン測定や性能改善を実施済みとはしない。

## 目的と終了条件

Warm Glass Table、1920x1009で感じる低い画像更新頻度について、GPU処理の長さと、
Present/VSync・フレーム資源再利用待ち・RR完了確認/公開の遅延を区別する。
調査中はフェンス位置・待機方式・描画スケジュールを変更しない。

同じproductionのGPU処理、検証結果の読み戻し、CPU完了観測、公開、Present採用を
対応づけ、主要な遅延区間を反復測定で説明できれば調査完了とする。最終成果物は
証拠付きの原因整理と、production完了フェンス分離を含む次の変更の採否判断。
トリプルバッファ、毎表示フレームPT/RR、アップスケールの実装は今回の範囲外。

## 現行順序のコード確認

通常描画の主要な順序:

1. RenderFrame: PopulateCommandList / ExecuteCommandLists。
2. RenderFrame: Present(syncInterval)。
3. MoveToNextFrame: 現在のフレーム用フェンスをSignal。
4. MoveToNextFrame: 次backbufferの資源が使用中ならWaitForFenceValue。
5. RenderFrame: 戻ったsubmittedFenceValueをRRのMarkSubmittedへ渡す。
6. 次のRunFrame: Pollで完了を確認し、summary検証後にRR画像を公開。

Nonblocking ONの場合、4は保留として外側のOnIdleへ戻る。通常のblockingと同じ
時刻関係とは限らない。最初の再現はOFFとし、その後に同じscheduleでONを比較する。

MarkSubmittedはGPUへの提出そのものではなく、RR状態機械へのフェンス登録である。
また、Presentより後にSignalするというコード順だけで、VSyncがSignalを遅らせると
断定しない。CPU Present呼出時間、GPUキュー上の進捗、CPUの完了観測を実測する。

参照する実装:

- Engine/RtPbrSurveyEngine.cpp: RenderFrame、MoveToNextFrame、RunFrame、AcquireFrameResources。
- Engine/Rhi/Dx12/GraphicsDevice.cpp: ExecuteCommandLists、Present、SignalFence、WaitForFenceValue。
- Renderer/PathTracingRrLive.cpp: MarkSubmitted、Poll、公開確定。
- WorkMeter.h: GPU query/読み戻し。現在は主にpacket先頭からの相対時刻。

## 記録する識別子とイベント

常にCPU frame/host tick、GPU submission、RR production、epochを別IDとして扱う。
一つのserial productionが複数submissionへ分かれる場合は全submissionを保持する。
Presentが保持画像だけの場合も、その採用production IDを明示する。

| 区間/イベント | 記録内容 |
|---|---|
| 入力snapshot | production ID、epoch、姿勢snapshot ID、取得CPU時刻 |
| 記録開始/終了 | PT/RR/検証を含むパス、CPU時刻、入力/出力slot |
| ExecuteCommandLists前後 | submission ID、対象production、CPU呼出時刻 |
| Present前後 | Present ID、backbuffer index、採用RR ID、interval/flags、CPU呼出時刻 |
| Signal前後 | fence identity/value、submission ID、CPU呼出時刻 |
| 次資源の取得/待機 | 次backbuffer、待つretire fence、待機開始/終了、完了値、保留tick |
| MarkSubmitted | production ID、対応fence、登録時刻 |
| Poll | 呼出時刻、対象fence、観測完了値、pending/ready/epoch不一致等の結果 |
| summary検証/公開 | input/output summaryのserial・coverage・判定、公開RR ID、CPU時刻 |
| GPUパス境界 | raw GPU timestamp、周波数、submission ID、PT/RR/検証/readback copy等の境界 |

イベント名のsubmitted/completeはCPU登録・CPU観測・GPU進捗を区別する。
RR SDK終了、出力検証/summary copy終了、GPU packet終了も同じ時点とみなさない。
公開条件にはsummary読み戻し完了が必要であり、SDK処理終了だけでは不足する。

## 時計と計測の扱い

CPUイベントは共通の単調時計で取る。GPUはraw tickを失わずに保存し、同じキューの
GPU/CPU clock calibrationで対応を取る案を確認する。対応精度・サンプリング間隔も
残す。利用できない場合はGPUキャプチャ等の代替調査を検討し、対応できない
相対GPU時刻をCPUのPresent時刻から直接引き算しない。

フェンスが実際に到達した時刻は、CPUが初めて完了を見た時刻と一致しない。
既存Pollでは最後の未完了観測と最初の完了観測の区間として扱い、正確なGPU到達
時刻を測れたと表現しない。GPU packet終了とRR公開までの差にも、読み戻し、
hostスケジューリング、計測誤差が含まれる。

ログは診断オプションで有効化し、イベントをメモリへ蓄積して測定後に保存する。
パス毎のJSON生成やflushがクリティカルパスを変えないようにする。データ欠落、
バッファあふれ、時計対応の不成立は不完全な測定として明示する。

## 作業ステップ

初回調査の概算は6〜10時間。環境競合待ちや追加のGPUキャプチャ環境整備は別。

| UID | 作業 | 難易度 | 目安 | 完了条件 |
|---|---|---|---|---|
| PF-01 | 実設定の保存、呼出順とID対応の整理 | 低〜中 | 0.5〜1時間 | ユーザ条件・実行schedule・入力プロファイルを保存し、production/submission/fence/Presentの対応表を作る |
| PF-02 | opt-in CPUイベント記録と整合性チェック | 中 | 2〜3時間 | Present/Signal/Wait/MarkSubmitted/Poll/公開/採用IDを記録し、同じ単調時計・IDで関連付けられる。診断OFFの動作を維持 |
| PF-03 | GPU時刻とCPU時刻の対応、計測負荷確認 | 中〜高 | 1〜2時間 | GPU処理/summary copy終了とCPU区間を区別できる。時計対応の精度・欠測、ログON/OFF差を報告 |
| PF-04 | Debug安全確認と実解像度Release比較 | 中 | 1.5〜2.5時間 | D3D12エラーなし。VSync/Nonblockingの比較を条件固定・反復で行い、タイムラインを保存 |
| PF-05 | 遅延区間の分類と変更案の採否 | 中 | 0.5〜1時間 | 代表フレーム図、集計とばらつき、未確定事項、次の最小変更をMDへまとめる |

## 測定条件

最初にユーザ実行のシーン、評価設定、CLI、build/commit、SDK/driver、SPP/bounces、
乱数seed、ガラス実験フラグ、表示/入力解像度、VSync、実際のeager/safe-serial/chainedを
保存する。未保存の設定を推測で1 spp/16 bounces等へ置き換えない。
Debugは安全性確認、Releaseは性能測定として分離する。

主測定は同じシーン・入力プロファイル・schedule・1920x1009で次の4条件:

| 条件 | VSync | Nonblocking |
|---|---|---|
| A | ON | OFF |
| B | OFF | OFF |
| C | ON | ON |
| D | OFF | ON |

まずA/BをABBAで反復し、次に必要なC/Dで再利用待ちの影響を確認する。
採用する反復数と観測時間は測定前に固定する。初期案は各ABBAを2組、各runの
安定化後に30〜60秒の固定実時間で観測する。静止を先に測り、固定したカメラ
移動/停止シーケンスでも再確認する。フレーム数固定だけで観測時間を決めない。

eager対safe-serial、safe-serial対chainedは別の比較として扱う。この主測定の
VSync/Nonblocking比較と入力プロファイル変更を混ぜない。chainedによる中間admission
削減を調べる追加測定は、主測定の結果を受けてRRI-03へ接続する。

他workspaceのGPUプロセスは勝手に終了しない。競合があれば準備まで進め、測定を
保留する。GPU負荷・クロック・温度等の状況を記録し、起動/SDK初期化、キャプチャ、
ログ保存、終了時drainの時間を測定区間から除く。

## 仮説と判定材料

| 仮説 | 観測する材料 | 判定上の注意 |
|---|---|---|
| Present呼出がSignal発行を遅らせる | Present前後、Signal発行、時計対応したGPU処理終了、A/B差 | 呼出順だけでは原因確定しない |
| 再利用待ちがRR登録/次の処理を遅らせる | SignalからMarkSubmittedまでと、その間のretire fence待機 | Signal済みGPU処理とCPU側登録遅延を分ける |
| 完了済みだがPoll/公開が遅い | 完了観測の区間、Poll頻度、公開、Present採用 | 公開数と新しいRR画像の提示数を混同しない |
| GPU実処理が支配的 | 同じproductionのPT、RR、検証/copyとqueue gap、クロック状況 | GPU idleとCPU fence waitは同じものではない |
| 直列admission/表示専用tickに空白がある | productionをまたぐsubmit/完了/次の投入の対応 | 追加のsafe-serial/chained比較で確認する |

CPU/GPUの重複する区間を単純加算せず、時刻をそろえたクリティカルパスで説明する。
GPU時間が長くなった場合も、別母集団・周波数・競合・設定差を排除する前に
NonblockingやVSyncだけを原因と断定しない。

## 安全条件と今回行わないこと

- 既存のframe-retire fenceの位置/意味を調査途中に置き換えない。
- RR公開をGPU完了・summary検証より早めない。不正出力公開0を維持する。
- 未完了のallocator/CB/descriptor/readback/出力slotを再利用しない。
- resize、pause/resume、backend切替、epoch変更で記録の対応が壊れないことを確認する。
- 前置きの診断フェンスが必要になった場合も、独立した診断資源として別途設計し、
  既存の資源寿命管理フェンスや共有auto-resetイベントを安易に流用しない。
- この調査だけでトリプルバッファやアップスケール実装へ着手しない。

## 成果物と次の判断

- 実設定manifest、イベント仕様、ID対応と整合性チェック結果。
- 代表productionのCPU/GPUタイムラインと、全観測区間の分布/p50/p95。
- Present提出頻度、新しいRR画像を採用したPresent頻度、公開頻度、姿勢の古さ。
- A/Bおよび必要なC/Dの反復比較、計測負荷・時計誤差・未確定事項。
- 原因と次の最小変更を記した調査結果MD。生ログはPC内のignored build配下へ保存。

同期/公開の遅延が支配的なら、production完了と資源retireの分離を含むRRI-06の
統合パイプライン設計へ進む。GPU実処理が支配的ならRRI-04/05を優先する。
両方が効いていれば影響の大きい区間から変更する。証拠が不足している場合は
原因を断定せず、次に必要な測定を具体化して調査結果に残す。
