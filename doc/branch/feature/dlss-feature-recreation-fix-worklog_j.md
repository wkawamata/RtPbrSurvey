# DLSS Feature Recreation Fix 作業ログ

## 2026-08-29: Unsupported quality mode監査

- 報告されたUI操作、DLSSを有効化し、DLAAからUltra Qualityへ切り替えるhost pathを確認した。
- Streamline logは未処理例外の前にNGX feature作成失敗 `0xbad00010` を繰り返し記録していた。
- hostのmode mapping、optimal-settings query、render-size再作成、frameごとの `slDLSSSetOptions`／`slEvaluateFeature` pathを監査した。
- application quality-mode contractとUIからUltra Qualityを削除した。NVIDIAの公開Streamline sampleが選択可能にするのはDLAA、Quality、Balanced、Performance、Ultra Performanceであり、active NGX runtimeがunsupported parameterとして拒否するSDK enumをhost UIへ公開しない。
- このhotfixはHybrid Reflection denoiser contractとは独立している。
- Debug x64 buildはerror 0件、既知のvcpkg重複import warning 1件で成功した。
- runtime acceptanceは合格した。Ultra Qualityが表示されず、DLAA、Quality、Balancedの選択経路で報告されたNGX feature作成失敗または例外が発生しないことを確認した。

Status: done
