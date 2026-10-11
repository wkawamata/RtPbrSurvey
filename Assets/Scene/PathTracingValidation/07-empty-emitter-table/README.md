# 07-empty-emitter-table

発光三角形がゼロでも正常に描画できることを確認する。一定白色の環境光で床が明るくなること。BSDF / NEE / MIS の切り替えで画像が変わらないこと。空の発光テーブルではNEEを使わずBSDFへフォールバックする。

Reproduce with Tests/PathTracing/validate_emissive_controls.py.
Raw HDR captures and runtime logs stay under bin/PathTracingValidation.
