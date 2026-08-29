# Hybrid Reflection Denoiser Contracts 作業ログ

## 2026-08-29: Contract抽出開始

- `main` の `16c8ca6` から `features/hybrid-reflection-denoiser-contracts` を開始した。
- 実装済みtemporal resource、ping-pong ownership、reset、depth／normal rejection、moments、persistent confidence、bounded surface-filter実験を監査した。
- 実装済みtemporal behaviorと将来のedge-aware spatial pass境界を分離したfocused denoiser contractを抽出した。
- 将来の `ReflectionDenoisedRadiance` を独立したstateless spatial outputとして固定した。最初のspatial実装はtemporal historyを上書きせず、feedbackもしない。
- 未加重radiance境界と最終 `LightPass` ownershipは変更しない。
- 古いresource table記述を修正した。`ReflectionResolvedRadiance.a` は固定値 `1` ではなく、実装済みTemporal Validity診断コードを保持する。RGB semanticsは変更しない。
- 検証はcontractとcodeの照合、Markdown link／whitespace確認、日英worklog一致確認に限定した。code／shader変更がないためDebug x64 buildとruntime validationは再実行していない。

Status: done
