"""Part 3: scale-invariant illumination, finite visibility, contact bias and self-hit diagnostics."""
import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
from types import SimpleNamespace

from compare_hdr import capture, read_pfm, rmse
from validate_part1 import write_json


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "Assets/Scenes/PathTracingValidation/single-light-visibility"
SCALES = [0.01, 1.0, 100.0]


def floor_point(x, y):
    # Independent pinhole construction in normalized medium-scene coordinates.
    length = math.sqrt(65)
    forward = [0, -4/length, 7/length]
    up = [0, 7/length, 4/length]
    tangent = math.tan(math.radians(55/2))
    horizontal = (2*x/1920-1)*tangent*1920/1080
    vertical = (1-2*y/1080)*tangent
    direction = [horizontal, forward[1]+up[1]*vertical, forward[2]+up[2]*vertical]
    t = -4/direction[1]
    return [t*direction[0], 0, -7+t*direction[2]]


def segment_hits_cube(origin, destination):
    minimum, maximum = 0.0, 1.0
    for start, end, lower, upper in zip(origin, destination, [-.5, 0, -.5], [.5, 1, .5]):
        direction = end-start
        if abs(direction) < 1e-12:
            if not lower <= start <= upper:
                return False
        else:
            a, b = sorted(((lower-start)/direction, (upper-start)/direction))
            minimum, maximum = max(minimum, a), min(maximum, b)
            if minimum > maximum:
                return False
    return True


def contact_roi_contract():
    points = [floor_point(x, y) for x in (1060, 1188) for y in (524, 556)]
    visible = all(not segment_hits_cube([0, 4, -7], point) for point in points)
    return dict(primaryRaysMissCube=visible, normalizedFloorCorners=points,
        note="Independent pinhole/AABB corner check; continuous ROI lies to the right of cube silhouette.")


def make_case(scale, angle="angled", variant="clear", policy="relative", family="visibility"):
    scene = json.loads((ASSETS / "scene.json").read_text())
    preset = json.loads((ASSETS / "render-preset.json").read_text())
    light = preset["lighting"]["lights"][0]
    light["position"] = [-2, 3, 0] if angle == "angled" else [0, 3, 0]
    blocker = scene["nodes"][1]
    if variant == "clear":
        scene["nodes"] = scene["nodes"][:1]
    elif variant == "between":
        blocker["translation"] = [v * .5 for v in light["position"]]
    elif variant == "beyond":
        # Same physical slab at every bias policy; bottom is strictly beyond the light.
        blocker["translation"] = [light["position"][0], 3.00015, 0]
        blocker["scale"] = [10, .0001, 10]
    elif variant == "contact":
        blocker["translation"] = [0, .5, 0]
        blocker["scale"] = [1, 1, 1]
    if family == "self":
        sphere = copy.deepcopy(blocker)
        sphere.update(id="convex-sphere", name="Convex sphere", translation=[0, .7, 0], scale=[1, 1, 1])
        sphere["primitive"].update(kind="sphere", radius=.7, stacks=32, slices=48)
        scene["nodes"] = [sphere]
        scene["camera"].update(position=[0, 1.5, -4], target=[0, .7, 0])
        light["position"] = [-2, 3, -2]
    for node in scene["nodes"]:
        node["translation"] = [v * scale for v in node["translation"]]
        node["scale"] = [v * scale for v in node["scale"]]
    camera = scene["camera"]
    for key in ("position", "target"):
        camera[key] = [v * scale for v in camera[key]]
    for key in ("nearZ", "farZ", "orthographicHeight"):
        camera[key] *= scale
    light["position"] = [v * scale for v in light["position"]]
    light["range"] *= scale
    light["intensity"] *= scale * scale
    # Keep TMax proportional in all policies; isolate origin bias and TMin separately.
    preset["shadow"].update(normalBias=.01 * (scale if policy in ("relative", "fixed-tmin") else 1),
        rayTMin=.001 * (scale if policy in ("relative", "fixed-bias") else 1), rayTMax=10000 * scale)
    if policy == "zero":
        preset["shadow"].update(normalBias=0, rayTMin=0)
    if variant == "unshadowed":
        preset["shadow"]["enabled"] = False
    preset["pathTracing"].update(maxBounces=1, debugOutput=3, environmentSamplingMode=0)
    scene["sceneId"] = f"scale-{family}-{scale}-{angle}-{variant}-{policy}"
    scene["name"] = "PT " + scene["sceneId"]
    return scene, preset


def compare_visibility(clear, blocked, beyond):
    average = sum(clear) / len(clear)
    return dict(clearMean=average, blockedMean=sum(blocked)/len(blocked),
        blockedRatio=sum(blocked)/sum(clear) if sum(clear) else None,
        beyondMaxDifference=max(abs(a-b) for a,b in zip(clear,beyond)),
        passed=average > .001 and sum(blocked) < .8 * sum(clear) and
            max(abs(a-b) for a,b in zip(clear,beyond)) <= 1e-6)


def normalized_profile(image, clear, width):
    height = len(image)//(width*3)
    profile=[]
    for x in range(width):
        numerator=sum(image[(y*width+x)*3+c] for y in range(height) for c in range(3))
        denominator=sum(clear[(y*width+x)*3+c] for y in range(height) for c in range(3))
        profile.append(numerator/denominator if denominator > 1e-12 else None)
    return profile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--smoke",action="store_true")
    parser.add_argument("--samples",type=int,default=4)
    parser.add_argument("--timeout",type=int,default=180)
    args=parser.parse_args()
    if not 1 <= args.samples <= 4096 or args.timeout <= 0:
        parser.error("Use 1-4096 samples and a positive timeout")
    output=args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Use an empty output directory to retain all failed attempts")
    output.mkdir(parents=True,exist_ok=True)
    report=dict(schemaVersion=1,part=3,generatedUtc=datetime.now(timezone.utc).isoformat(),
        commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
        branch=subprocess.check_output(["git","branch","--show-current"],cwd=ROOT,text=True).strip(),
        workspace=str(ROOT),build="Debug x64",samples=args.samples,seed=7,scales=SCALES,
        scaleIllumination="Position/range/camera/geometry scaled by s; intensity scaled by s^2",captures=[],
        failures=[],visibility=[],contact=[],selfIntersection=[],status="running",
        thresholds=dict(minClearMean=.001,maxBlockedRatio=.8,maxBeyondDifference=1e-6,
            relativeGeometryRadianceTolerance=.01,shadowProfileCutoff=.5,leakRatioCutoff=.9),
        rois=dict(visibility=[944,524,32,32],contact=[1060,524,128,32],selfIntersection=[936,516,48,48]))
    report["contactRoiContract"] = contact_roi_contract()
    if not report["contactRoiContract"]["primaryRaysMissCube"]:
        raise ValueError("Contact ROI includes the blocker silhouette")
    try:
        report["gpuDriver"]=subprocess.check_output(["nvidia-smi","--query-gpu=name,driver_version","--format=csv,noheader"],text=True,timeout=10).strip()
    except (OSError,subprocess.SubprocessError) as error:
        report["gpuDriverQueryError"]=str(error)
    write_json(output/"report.json",report)
    images={}
    def run(scale,angle,variant,policy,family="visibility"):
        scene,preset=make_case(scale,angle,variant,policy,family)
        name=scene["sceneId"]
        scene_path=output/(name+"-scene.json"); preset_path=output/(name+"-preset.json")
        scene["renderPreset"]=preset_path.name
        write_json(scene_path,scene); write_json(preset_path,preset)
        roi=report["rois"]["selfIntersection" if family=="self" else "visibility"]
        request=SimpleNamespace(root=ROOT,exe=ROOT/"bin/x64/Debug/RtPbrSurvey.exe",scene_file=scene_path,
            render_preset=preset_path,output=output,timeout=args.timeout,roi=roi)
        from compare_hdr import build_capture_command
        command=build_capture_command(request,0,7,args.samples,output/(name+".pfm"),output/(name+".log"))
        try:
            record,pixels=capture(request,0,7,args.samples,name)
            if record["dimensions"]!=(1920,1080) or record["diagnostics"]["maxBounces"]!=1:
                raise ValueError("Wrong dimensions or bounce limit")
            record.update(name=name,scale=scale,angle=angle,variant=variant,policy=policy,family=family,
                command=command,sceneHash=hashlib.sha256(scene_path.read_bytes()).hexdigest(),
                presetHash=hashlib.sha256(preset_path.read_bytes()).hexdigest(),shadow=preset["shadow"],
                camera=scene["camera"],light=preset["lighting"]["lights"][0],roi=roi)
            report["captures"].append(record)
            images[(scale,angle,variant,policy,family)]=(record,pixels)
            return record,pixels
        except Exception as error:
            report["failures"].append(dict(name=name,command=command,error=str(error)))
            report["status"]="failed"
            raise
        finally:
            write_json(output/"report.json",report)
    scales=[1.0] if args.smoke else SCALES
    for scale in scales:
        for angle in ("front","angled"):
            for policy in (["relative"] if args.smoke else ["relative","fixed"]):
                group=[run(scale,angle,v,policy)[1] for v in ("clear","between","beyond")]
                report["visibility"].append(dict(scale=scale,angle=angle,policy=policy,**compare_visibility(*group)))
    if not args.smoke:
        for scale in scales:
            clear_record,_=images[(scale,"angled","clear","relative","visibility")]
            _,clear=read_pfm(Path(clear_record["path"]),report["rois"]["contact"])
            profiles={}
            for policy in ("relative","fixed","fixed-bias","fixed-tmin"):
                record,_=run(scale,"angled","contact",policy,"contact")
                _,pixels=read_pfm(Path(record["path"]),report["rois"]["contact"])
                profiles[policy]=normalized_profile(pixels,clear,128)
            baseline=profiles["relative"]
            shadow_columns=[i for i,v in enumerate(baseline) if v is not None and v<.5]
            for policy,profile in profiles.items():
                leak=sum(profile[i]>.9 for i in shadow_columns)/len(shadow_columns) if shadow_columns else None
                report["contact"].append(dict(scale=scale,policy=policy,roi=report["rois"]["contact"],
                    profile=profile,baselineShadowColumnCount=len(shadow_columns),baselineShadowLeakFraction=leak,
                    shadowColumnCount=sum(v is not None and v<.5 for v in profile)))
        for scale in scales:
            _,clear=run(scale,"angled","unshadowed","relative","self")
            for policy in ("relative","zero"):
                _,pixels=run(scale,"angled","shadowed",policy,"self")
                valid=[(a,b) for a,b in zip(pixels,clear) if b>1e-8]
                report["selfIntersection"].append(dict(scale=scale,policy=policy,roi=report["rois"]["selfIntersection"],
                    rmse=rmse(pixels,clear),meanRatio=sum(a for a,b in valid)/sum(b for a,b in valid),
                    darkenedChannelFraction=sum(a<.9*b for a,b in valid)/len(valid)))
    report["status"]="complete"
    report["visibilityPassed"]=all(r["passed"] for r in report["visibility"])
    write_json(output/"report.json",report)
    print(json.dumps(dict(status=report["status"],captures=len(report["captures"]),visibilityPassed=report["visibilityPassed"])))


if __name__=="__main__":
    main()
