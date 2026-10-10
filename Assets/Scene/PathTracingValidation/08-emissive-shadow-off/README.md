# 08-emissive-shadow-off

影OFF時の発光サンプリングのフォールバックを確認する。BSDF / NEE / MIS の出力が同一で、床は発光面からのBSDF経路で明るくなること。環境光と直接光は無効。影をONに戻すと通常のNEE/MIS動作へ戻る。

Reproduce with Tests/PathTracing/validate_emissive_controls.py.
Raw HDR captures and runtime logs stay under bin/PathTracingValidation.
