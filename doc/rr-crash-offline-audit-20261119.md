# RRクラッシュ: オフライン監査結果と次の調査

日付: 2026-11-19 / Work4 / codex/rr-eager-production
状態: 原因未特定。GPU検証停止を維持。PF-04/PF-05は未完了。
事故の一次記録は[こちら](rr-pf-windows-crash-20261119.md)。

## 今回の結論

- ユーザ報告では最大化での暗転・フリーズはPF計測の少し前にも発生した。
  計測器だけを原因と断定できない。既存のRR/リサイズ経路も調査対象とする。
- 既存ダンプの0x133にはNVIDIA DPC経路がある。アプリやドライバのどちらに
  原因があるか、最新の手動再起動と同一原因かは未確定。
- GPU待機前にRR資源を解放する明白な箇所は、今回読んだ経路では見つからない。
  コードの順序確認であり、実行時の全資源の安全性を証明したものではない。
- 完了済みトレースには、定常時の繰り返しリサイズ/履歴リセットを示す記録はない。
  未完了で失われたクラッシュ時の挙動には適用できない。
- 実行コードは変更していない。推測でスケジューリングや解放順序を変えない。

## ダンプから分かる範囲

Microsoft公開カーネルシンボルを使い、ローカルDbgEngで読み取りを実施した。
64のエンジンコンテキストを列挙したが、スタックを取得できたのは55のみ。
ミニダンプ自体もレジスタ/スタック中心の保存範囲であり、全CPUの処理や
DPC watchdogの履歴全体を再構成できていない。解析拡張の!analyzeも未使用。

ローカルNVIDIAイメージのCodeViewから得たPDB識別子に対し、Microsoft公開
シンボルサーバーへHEAD照会した結果はHTTP 404。ダンプを送信する操作ではない。
この配信先から一致するPDBを取得できなかったことを意味し、他の配信先の存在や
シンボルの公開方針まで断定するものではない。NVIDIA内部は現在モジュール+
オフセット表記までで、特定の関数の不具合とは判断できない。

## 資源とフェンスの確認

| 対象 | コードで確認したこと | 残る限界 |
| --- | --- | --- |
| リサイズ入口 | AppのOnWindowSizeChangedは遅延リサイズを要求。EngineのRunFrameはフレーム資源取得後にApplyResize | 最大化直前の実際の呼び出し履歴は未保存 |
| GPU drain | ApplyResizeはFlushGpu後にRR SDK資源を解放し、backbuffer/寸法依存資源を更新 | fence待機の異常終了やdevice removalを含む実行証拠はない |
| FlushGpu | フレームのfence値と完了値からSignal値を選び、同じ値を待って全フレームの次値を更新 | 複数queueを導入する場合は別途所有権設計が必要 |
| nonblocking取得 | 保留中はretire fenceを保持し、取得後に次のSignal値へ置き換える | 実行時の全履歴を今回のミニダンプから復元できない |
| RR ticket/epoch | PathTracingRrLive::Pollは未submit/未完了ticketのdrain後にepochを更新し、旧入力/出力ComPtrを解放 | 実際の各tag/descriptorの利用末端をダンプで追えていない |
| SDK allocation | allocationフラグはslEvaluateFeatureを呼ぶ前に立てる。evaluate失敗後も解放対象になる | SDK内部の処理は不透明 |
| SDK release | 初期化済み・allocationありの場合にslFreeResources。失敗は呼び出し側へ返す | 成功応答はドライバ内部の安全性の証明ではない |
| tagged state | RR入力のgraph宣言とSDK tagはPS_RESOURCE、出力はUAV。検証用遷移は元へ戻す構造 | 他経路の変更や実行時barrier検証は未実施 |
| shutdown | フレーム資源の完了待ち後にSDKを終了する構造 | 不完全終了時の最終到達点は未保存 |

参照箇所:

- App/RtPbrSurveyApp.cpp: OnWindowSizeChanged
- Engine/RtPbrSurveyEngine.cpp: RunFrame, ApplyResize, FlushGpu,
  AcquireFrameResources, MoveToNextFrame, Shutdown
- Engine/Rhi/Dx12/GraphicsDevice.cpp: WaitForFenceValue
- Renderer/PathTracingRrLive.cpp: Poll, MarkSubmitted
- Renderer/StreamlineAdapter.cpp: native evaluate, RR resource release, shutdown
- ローカルSDK資料: ProgrammingGuideDLSS_RR.mdの解放例では、pending evaluateの
  command listをflushしてからslFreeResourcesを呼ぶ。現行の基本順序はこれに沿う。

WaitForFenceValueはOS待機の返値を検査せず、待機後のfence値も再検査していない。
異常検出強化の候補だが、今回この待機が失敗した証拠はない。原因修正としては
扱わず、実装するなら待機結果・要求値・完了値・device removed reasonを記録し、
異常時に資源再利用を止める独立した変更としてレビューする。

## 既存トレースの再集計

完了済み7本のうち、後期の5本はepoch-change計測と最終reset数を持つ。
いずれもactive epoch=14、初期epoch変更1回、SDK履歴reset=1だった。
前期2本は最終reset数などの計測がないため、この比較から除いた。

後期5本のRR入力数は56 / 417 / 195 / 840 / 202。
これは定常実行中にresetを繰り返したという仮説を支持しないが、最大化操作の
試験でも、クラッシュした未完了runの証拠でもない。性能比較コホートとしては
引き続き不採用。既存の生データを読むだけで、新しいGPU実行は行っていない。

## 計測コードの監査

RrFenceTraceはCPUイベント50万件、GPUポイント20万件、calibration256件で上限を
設ける。GetClockCalibration/GetTimestampFrequencyはcapture有効時に約1秒間隔。
イベント記録自体はSignal/Wait/Executeの順序を変更しない。

runnerのnvidia-smiは1秒間隔の別プロセス。計測器もtelemetryもドライバに触れる
追加要因ではあるため、CPUメモリ記録だけと同等とはみなさない。既存の
timing-onlyはGPU query marker/calibrationを省くが、telemetryも同時に外す比較は
別途設計が必要。現状のGPU holdはrunner入口と各run入口で確認される。

## 次の作業案と再開条件

1. GPUを使わず、必要最小限の障害記録を設計する。対象は起動、RR有効化、
   resize要求/実行、GPU drain、slFreeResources、ResizeBuffers、資源再構築、
   終了の開始/終了。短いローカル記録を各境界でflushし、強制再起動後にも
   最終到達点を残す。これはPFの終了時一括保存と目的が異なる。
2. fence待機の異常判定と上記記録は原因修正と分けて、小さく実装・静的レビュー
   する。ホットパスの毎フレーム同期I/Oや新しいGPU queryは追加しない。
3. GPU再開前に、記録内容・一回の試験の操作/時間・停止基準を明示して合意する。
   最初からGlass+RR+最大化をまとめて繰り返さない。起動、RR有効化、最大化を
   別の段階にし、前段階で異常が出たら後段階へ進まない設計とする。
   RRなし/あり、計測なし/ありを同時に変えず、telemetryも既定で開始しない。
4. Windows全体のフリーズが再発した場合に自動再試行しない。ドライバ変更、
   TDR設定変更、再起動は本作業から自動実行しない。安全な再現を保証できない
   場合はこのPCでの負荷試験を継続せず、既存証拠での調査/外部解析を検討する。

この手順は試験開始の許可ではない。GPU holdは解除せず、PRもDraftのまま保持する。
PF-05の性能変更採否は、安定性の問題を整理し、必要な反復測定が成立してから判断する。

## 境界ログの実装（2026-11-20）

調査用ビルドでは起動オプション `-RrCrashLog <path>` で記録を有効にする。
親フォルダを事前に作成し、試験ごとに新しいローカルファイル名を使う。
既存ファイルがある場合は起動時にエラーとなる。各行はUTC時刻、連番、
イベント名、補足値3個の順で、書き込みごとにFlushFileBuffersを呼ぶ。

- `app.init.*`、`streamline.init.*`、`graphics.init.*` で起動時の到達点を確認する。
- `rr.settings` にはRR有効とNative Evaluate有効の設定値を記録する。
  `pt.reconstruction` はPTの再構成バックエンドの変更前後を記録する。
  `rr.evaluate.first.*` はSDK資源が未確保の状態から最初に評価APIを呼んだ前後を表す。
- `resize.request` と `resize.begin` は出力寸法を記録する。
  `gpu.flush.*` はSignal、待機、フェンス値を示す。
  `rr.resources.free.*` と `swapchain.resize_buffers.*` は各APIの呼び出し前後を示す。
  `resize.resources.begin` は描画資源更新の入口と新しい描画寸法、
  `resize.end` は資源更新を含むリサイズ全体の終了を示す。
- 終了時はアプリ、レンダラー、GPU待機、SDK終了の境界を記録する。
  SDK/DXGI呼び出しの結果を持つ `.end` 行では、最初の補足値がAPI戻り値。
  `gpu.flush.wait.end` は既存の待機関数が戻ったことを示す。

停止後は最後の完全な行と、直前に同じ操作の開始・終了があるかを確認する。
CPU側で最後に記録できた位置を示すため、GPU内部の停止位置は別の証拠で確認する。
実行中の書き込みやflushに失敗した場合はデバッガに通知し、ログを閉じる。
ディスクへのflushは操作にかかる時間を変えうるため、性能測定時はこのログを外す。

## 通常起動の確認（2026-11-20）

ユーザ承認を得て、診断ログ付きのDebugビルド（090a2f2）を1回起動した。
隔離したAPPDATA、960×540、RR無効でトップメニューを表示し、通常終了した。
`app.init.end` と `app.shutdown.end` を確認。初回表示時に同寸法の
リサイズ要求があり、GPU待機と描画資源の更新が完了した。
今回のログには `ResizeBuffers`、RR評価、RR資源解放の呼び出しは現れていない。
GlassシーンでのRR有効化、最大化、およびPF計測は保留する。
ログは `build/pf-crash-incident-20261119/startup-baseline-20261120/rr-crash.log`
に保存した。

## 固定サイズでのGlassシーンとRRの確認（2026-11-20）

Debugビルド（090a2f2）で、保存済み設定から再構成した
PF persisted Warm Glass Tableを960×540で1回起動した。
初回のRR SDK評価は01:24:26 JSTに完了し、戻り値は0。
固定サイズのウィンドウでGlassシーンの表示を確認した。この試験で
確認したのは初回評価APIの戻り値と画面表示まで。

自動の終了操作はタイムアウトしたが、ユーザが同じウィンドウを手動で閉じた。
01:32:41 JSTにGPU待機完了、RR資源解放とStreamline終了の戻り値0、
アプリの通常終了を確認した。固定サイズでの起動・初回RR評価・終了は合格。
最大化とPF計測は保留し、同じ試験の再起動は行っていない。
ログは build/pf-crash-incident-20261119/glass-rr-fixed-window-20261120/rr-crash.log
に保存した。

## RR無効での小幅なリサイズ確認（2026-11-20）

ユーザ承認のうえ、Debugビルド（090a2f2）を隔離したAPPDATAで起動した。
RR無効のトップメニューで上枠を動かし、クライアント領域を960×540から
960×504に一度変更した。GPUフェンス1461の待機完了後、
ResizeBuffersの戻り値0、resize.end、app.shutdown.endを確認した。
この条件で実寸変更と通常終了は合格。RR有効時の実寸変更、最大化、
PF性能計測は未実施。
ログは build/pf-crash-incident-20261119/native-resize-20261120/rr-crash.log
に保存した。

## Warm Glass Table＋RRでの小幅なリサイズ確認（2026-11-20）

Debugビルド（090a2f2）、隔離したAPPDATA、PF persisted Warm Glass Tableを使用。
初回のRR SDK評価成功後、上枠を1回動かし、960×540から960×504に変更した。
GPUフェンス296の待機、旧RR資源解放、ResizeBuffers、資源再構築、変更後の
初回RR SDK評価を順に確認した。API戻り値はいずれも0。変更後のGlassシーンの
表示と、再度のRR資源解放を含む正常終了も確認した。
小幅なリサイズ試験はこの条件で合格。最大化とPF性能計測は保留。
ログは build/pf-crash-incident-20261119/glass-rr-small-resize-20261120/rr-crash.log
に保存した。

## RR無効での最大化確認（2026-11-20）

ユーザ承認のうえ、Debugビルド（090a2f2）を隔離したAPPDATAで起動した。
RR無効のトップメニューを一度だけ最大化し、960×540から3840×2089に変更した。
GPUフェンス432の待機完了、ResizeBuffersの戻り値0、描画資源の再構築、
最大化後のトップメニュー表示、通常終了を確認した。
この条件で最大化は合格。GlassシーンでのRR有効時の最大化とPF性能計測は未実施。
ログは build/pf-crash-incident-20261119/native-maximize-20261120/rr-crash.log
に保存した。

## Warm Glass Table＋RRでの最大化確認（2026-11-20）

Debugビルド（090a2f2）、隔離したAPPDATA、PF persisted Warm Glass Tableを使用。
960×540で初回RR評価が完了した後、RR有効のまま一度だけ3840×2089へ最大化した。
GPUフェンス488の待機、旧RR資源解放、ResizeBuffers、描画資源更新を順に確認した。
最大化後の最初のRR評価の戻り値は0。終了時もGPU待機、RR資源解放、
Streamline終了、アプリの通常終了まで記録された。今回の試行で暗転・フリーズは
再現しなかった。判定範囲は最大化後の初回評価APIと通常終了まで。
既知のクラッシュ原因は未特定で、PF-04/PF-05と自動計測の保留は継続する。
ログは build/pf-crash-incident-20261119/glass-rr-maximize-20261120/rr-crash.log
に保存した。

## ローカル証拠

build/pf-crash-incident-20261119/ 以下のdump-all-contexts.txt、
nvidia-symbol-info.json、completed-trace-lifecycle-summary.jsonを追加参照。
ダンプ・ログ・生トレースはPC内に保存し、Gitには本文書だけを追加する。
