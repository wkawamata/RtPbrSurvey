# 06-blocker-beyond-emitter

光源より奥の遮蔽物がNEEの可視性判定を妨げないことを確認する。発光面は高さ3、遮蔽物は高さ4.5。各モードの出力は遮蔽物なしの01基準シーンと同一になること。環境光と直接光は無効。2バウンス、固定seedで検証。

Reproduce with Tests/PathTracing/validate_emissive_controls.py.
Raw HDR captures and runtime logs stay under bin/PathTracingValidation.
