# 05-backfacing-emissive-panel

発光面の背面から照明が漏れないことを確認する。BSDF / NEE / MIS の各モードで、床の中央ROIが完全に黒になること。発光面は高さ3のまま180度回転し、床に背面を向けている。環境光と直接光は無効。2バウンス、固定seedで検証。

Reproduce with Tests/PathTracing/validate_emissive_controls.py.
Raw HDR captures and runtime logs stay under bin/PathTracingValidation.
