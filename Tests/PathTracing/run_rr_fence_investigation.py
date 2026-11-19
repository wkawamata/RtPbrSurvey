"""Reproducible PF trace cohort. Preserves original APPDATA by isolating each child."""
import argparse
import ctypes
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from analyze_rr_fence_trace import analyze
from measure_rr_release import require_isolation

ROOT=Path(__file__).resolve().parents[2]


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def run(exe, baseline, out, label, vsync, nonblocking, seconds, warmup, timing_only=False, motion=False, case="Glass 01"):
    require_isolation()
    folder=out/label;folder.mkdir()
    appdata=folder/'appdata/RtPbrSurvey';appdata.mkdir(parents=True)
    for name in ['scene_config.json','evaluation_states.json']:
        (appdata/name).write_bytes((baseline/name).read_bytes())
    env=os.environ.copy();env['APPDATA']=str(appdata.parent)
    cmd=[str(exe),'-EvaluationCase',case,'-WindowSize','1920','1009',
         '-EnableDlssRayReconstruction','-EnableExperimentalNativeRayReconstruction',
         '-RrFenceTrace',str(folder/'trace.json'),'-RrFenceTraceWarmup',str(warmup),
         '-RrFenceTraceSeconds',str(seconds),'-LogToFile',str(folder/'d3d12.log')]
    if not vsync: cmd.append('-DisableVSync')
    if nonblocking: cmd.append('-NonblockingFrameAcquisition')
    if timing_only: cmd.append('-RrFenceTraceTimingOnly')
    if motion: cmd.append('-RrFenceTraceCameraMotion')
    info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=1
    start=datetime.now().astimezone().isoformat()
    child=subprocess.Popen(cmd,cwd=ROOT,env=env,startupinfo=info)
    try: code=child.wait(timeout=warmup+seconds+180)
    finally:
        if child.poll() is None: child.kill();child.wait()
    require_isolation()
    log=(folder/'d3d12.log').read_text(encoding='utf-8-sig')
    if code or '[ERROR]' in log or '[CORRUPTION]' in log:
        raise RuntimeError(f'{label}: process/debug failure {code}: '+log[-3000:])
    trace=json.loads((folder/'trace.json').read_text(encoding='utf-8'))
    result=analyze(trace)
    assert trace['metadata'].get('windowVisibleAtFirstTick') and not trace['metadata'].get('iconicAtFirstTick')
    assert result.get('sdkFailureCount',0)==0
    assert result.get('summaryFailureCount',0)==0
    assert all(trace['metadata']['finalRr'][k]==0 for k in ['cancelled','inputRejected','outputRejected','sdkFailures'])
    assert trace['metadata']['finalRr']['resets']==1
    assert (trace['metadata']['renderWidth'],trace['metadata']['renderHeight'])==(1920,1009)
    assert trace['metadata']['schedule']=='eager' and trace['metadata']['nonblocking']==nonblocking
    assert trace['metadata']['vsync']==vsync and result['freshPresentFps']>0
    qpc=ctypes.c_longlong()
    ctypes.windll.kernel32.QueryPerformanceCounter(ctypes.byref(qpc))
    clock_unix=time.time()
    row=dict(label=label,command=cmd,start=start,end=datetime.now().astimezone().isoformat(),
             clockQpc=qpc.value,clockUnix=clock_unix,qpcFrequency=trace['qpcFrequency'],
             windowStart=trace['windowStart'],windowEnd=trace['windowEnd'],
             exitCode=code,vsync=vsync,nonblocking=nonblocking,motion=motion,timingOnly=timing_only,
             traceSha256=sha(folder/'trace.json'),analysis=result)
    (folder/'analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(label,'Present',round(result['presentFps'],3),'freshRR',round(result['freshPresentFps'],3),flush=True)
    return row


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--case',default='Glass 01')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=30)
    parser.add_argument('--warmup',type=int,default=5)
    parser.add_argument('--cohort',choices=['smoke','static','motion','overhead'],default='static')
    parser.add_argument('--pairs',type=int,default=2)
    args=parser.parse_args()
    exe=args.exe.resolve(strict=True);baseline=args.baseline.resolve(strict=True);out=args.output.resolve()
    if out.exists() and any(out.iterdir()): parser.error('Output must be empty')
    out.mkdir(parents=True,exist_ok=True)
    report=dict(status='running',sourceCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                exeSha256=sha(exe),scriptSha256=sha(Path(__file__)),
                analyzerSha256=sha(Path(__file__).with_name('analyze_rr_fence_trace.py')),
                baselineSha256={n:sha(baseline/n) for n in ['scene_config.json','evaluation_states.json']},
                assetSha256={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'Assets/Scenes/WarmGlassTable').rglob('*') if p.is_file()},
                runtimeSha256={p.name:sha(p) for p in exe.parent.glob('*') if p.is_file() and (p.suffix=='.cso' or p.name.startswith(('sl.','nvngx_')))},
                seconds=args.seconds,warmup=args.warmup,cohort=args.cohort,runs=[])
    def save(): (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    save()
    sampler=None
    try:
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        hardware=open(out/'gpu.csv','w',encoding='utf-8')
        sampler=subprocess.Popen(['nvidia-smi','--query-gpu=timestamp,name,driver_version,clocks.gr,clocks.sm,clocks.mem,utilization.gpu,power.draw,temperature.gpu',
                                  '--format=csv','-l','1'],stdout=hardware,stderr=subprocess.STDOUT,startupinfo=startup)
        if args.cohort=='smoke': sequence=[(True,False,False)]
        elif args.cohort=='overhead': sequence=[(False,False,t) for t in [False,True,True,False]]
        else:
            sequence=[]
            for nb in ([False,True] if args.cohort=='static' else [False]):
                for pair in range(args.pairs):
                    for vs in ([True,False,False,True] if pair%2==0 else [False,True,True,False]):
                        sequence.append((vs,nb,False))
        for index,(vs,nb,timing) in enumerate(sequence):
            label=f'{index:02d}-vs{int(vs)}-nb{int(nb)}'+('-timing' if timing else '-trace')
            report['runs'].append(run(exe,baseline,out,label,vs,nb,args.seconds,args.warmup,timing,args.cohort=='motion',args.case))
            save()
        assert sha(exe)==report['exeSha256'],'Executable changed during cohort'
        assert all(sha(ROOT/n)==h for n,h in report['assetSha256'].items()),'Asset changed'
        assert all(sha(exe.parent/n)==h for n,h in report['runtimeSha256'].items()),'Runtime changed'
        assert all(sha(baseline/n)==h for n,h in report['baselineSha256'].items())
        report['status']='passed'
    except Exception as e:
        report.update(status='failed',error=str(e));raise
    finally:
        if sampler is not None: sampler.terminate();sampler.wait()
        if 'hardware' in locals(): hardware.close()
        save()


if __name__=='__main__': main()
