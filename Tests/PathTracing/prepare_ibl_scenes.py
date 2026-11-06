"""Prepare full procedural IBL variants without overwriting isolation fixtures."""
import argparse
import json
import os
from pathlib import Path

from capture_pt_hybrid_comparison import ROOT
from validate_sponza_opaque_visibility import capture
from validate_part1 import sha, write_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    base = ROOT / "bin/PathTracingValidation"
    sources = {
        "cornell": base / "cornell-ibl-visual-square-20261101/environment/hybrid-on",
        "sponza": base / "sponza-opaque-visibility-r2-20261104/on-inspect",
    }
    report = dict(status="running", source="ProceduralStudio, not asset HDR", records=[])
    try:
        for name, source in sources.items():
            folder = output / name
            folder.mkdir()
            scene = json.loads((source / "scene.json").read_text(encoding="utf-8"))
            preset = json.loads((source / "render-preset.json").read_text(encoding="utf-8"))
            for asset in scene["assets"]:
                asset["path"] = Path(os.path.relpath((source / asset["path"]).resolve(), folder)).as_posix()
            scene["environment"].update(iblEnabled=True, skyColor=[.35,.5,.8], groundColor=[.08,.08,.1],
                lightColor=[1,.95,.85], lightDirection=[-.4,.7,-.6], backgroundIntensity=1,
                lightIntensity=3, lightSize=.15, fillIntensity=.2, colorPanelIntensity=0)
            preset["lighting"].update(iblIntensity=1, diffuseIblEnabled=True, specularIblEnabled=True,
                skyboxEnabled=True, skyboxPreview=False, directLightEnabled=False, emissiveEnabled=False,
                lights=[], primaryShadowLightId=0)
            preset["hybridReflection"].update(enabled=False, contributionEnabled=False)
            preset["toneMap"] = dict(operatorIndex=2, exposure=1)
            preset["diffuseEnvironmentVisibility"].update(enabled=True, sampleCount=64,
                sampleRotationEnabled=True, spatialFilterEnabled=True)
            write_json(folder / "scene.json", scene)
            write_json(folder / "render-preset.json", preset)
            record = capture(ROOT / "bin/x64/Debug/RtPbrSurvey.exe", folder, frames=80)
            record.update(scene=name, diffuseIbl=True, specularIbl=True, skybox=True)
            report["records"].append(record)
            write_json(output / "report.json", report)
            print(name, "IBL captured", flush=True)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_json(output / "report.json", report)
    print(report["status"], report.get("error", ""), flush=True)
    return report["status"] != "complete"


if __name__ == "__main__":
    raise SystemExit(main())
