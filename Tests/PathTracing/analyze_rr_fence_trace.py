"""PF trace decoding. CPU times are QPC; GPU time uses measured queue calibration."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        return None
    if not all(math.isfinite(x) for x in values):
        raise ValueError('Nonfinite timing')
    return dict(count=len(values), mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[int((len(values)-1)*.95)], min=values[0], max=values[-1])


def gpu_qpc(tick, frequency, calibrations, qpc_frequency):
    valid = [c for c in calibrations if c['result'] == 0 and c['frequency'] == frequency]
    if not valid or frequency <= 0:
        raise ValueError('No compatible GPU clock calibration')
    c = min(valid, key=lambda c: abs(tick-c['gpu']))
    return c['cpu'] + (tick-c['gpu'])*qpc_frequency/frequency


def analyze(trace):
    assert trace['finished'] and not trace['overflow'], 'Incomplete/overflowed trace'
    freq = trace['qpcFrequency']
    start, end = trace['windowStart'], trace['windowEnd']
    assert freq > 0 and end > start
    seconds = (end-start)/freq
    report = dict(seconds=seconds, presentFps=trace['presents']/seconds,
                  freshPresentFps=trace['freshPresents']/seconds,
                  publishedFps=trace['published']/seconds,
                  metadata=trace['metadata'], timingOnly=trace['timingOnly'])
    if trace['timingOnly']:
        assert not trace['events'] and not trace['gpu'] and not trace['calibrations']
        return report
    events = trace['events']
    assert events and all(b['qpc'] >= a['qpc'] for a,b in zip(events, events[1:])), 'CPU clock regressed'
    measured = lambda e: start <= e['qpc'] < end
    ms = lambda delta: delta*1000/freq
    frames = defaultdict(dict)
    for e in events:
        frames[e['frame']].setdefault(e['event'], []).append(e)
    durations = defaultdict(list)
    first_present, snapshots, published = {}, {}, {}
    associations = []
    sequence = ['execute_begin', 'execute_end', 'present_begin', 'present_end', 'signal_begin', 'signal_end']
    for frame, ev in frames.items():
        if 'execute_begin' not in ev:
            continue
        for name in sequence:
            assert len(ev.get(name, [])) == 1, (frame, name)
        e = {name:ev[name][0] for name in sequence}
        assert all(e[b]['qpc'] >= e[a]['qpc'] for a,b in zip(sequence,sequence[1:])), 'Scheduling order changed'
        sub = e['execute_begin']['submission']
        assert sub > 0 and all(x['submission']==sub for x in e.values())
        if measured(e['execute_begin']):
            for out,a,b in [('executeCpuMs','execute_begin','execute_end'),
                            ('presentCpuMs','present_begin','present_end'),
                            ('submitToSignalMs','execute_end','signal_begin')]:
                durations[out].append(ms(e[b]['qpc']-e[a]['qpc']))
        sdk_begin = ev.get('rr_sdk_begin', [])
        sdk_end = ev.get('rr_sdk_end', [])
        if sdk_begin or sdk_end:
            assert len(sdk_begin) == len(sdk_end) == 1, 'Unpaired CPU RR SDK markers'
            sdk_begin_qpc, sdk_end_qpc = sdk_begin[0]['qpc'], sdk_end[0]['qpc']
            assert sdk_begin_qpc <= sdk_end_qpc <= e['execute_begin']['qpc'], 'CPU RR SDK order invalid'
            if measured(e['execute_begin']):
                durations['rrSdkCpuMs'].append(ms(sdk_end_qpc - sdk_begin_qpc))
                durations['rrSdkCpuEndToSubmitMs'].append(ms(e['execute_end']['qpc'] - sdk_end_qpc))
        if 'resource_wait_begin' in ev:
            a,b=ev['resource_wait_begin'][0],ev['resource_wait_end'][0]
            assert a['a']==b['a'] and b['c']>=b['a'] and b['qpc']>=a['qpc']
            if measured(e['execute_begin']): durations['resourceWaitMs'].append(ms(b['qpc']-a['qpc']))
        mark = ev.get('rr_mark_submitted', [])
        assert len(mark)<=1
        if mark:
            m=mark[0]
            assert m['a']==e['signal_end']['a'] and m['qpc']>=e['signal_end']['qpc']
            if 'resource_wait_end' in ev: assert m['qpc']>=ev['resource_wait_end'][0]['qpc']
            if measured(e['execute_begin']): durations['signalToRrRegisterMs'].append(ms(m['qpc']-e['signal_end']['qpc']))
        associations.append(dict(frame=frame,submission=sub,production=e['execute_end']['production'],epoch=e['execute_end']['epoch'],
                                 executeQpc=e['execute_begin']['qpc'],executeEndQpc=e['execute_end']['qpc'],
                                 presentQpc=e['present_begin']['qpc'],presentEndQpc=e['present_end']['qpc'],
                                 signalQpc=e['signal_end']['qpc'],fence=e['signal_end']['a'],
                                 registerQpc=mark[0]['qpc'] if mark else None,
                                 rrPhase=mark[0]['b'] if mark else None))
        for snap in ev.get('input_snapshot', []):
            pose = ev.get('pose_ready', [snap])[-1]
            snapshots[(snap['a'],snap['b'])] = (pose['qpc'], snap['qpc'])
    for e in events:
        if e['event']=='published': published[(e['a'],e['epoch'])]=e['qpc']
        if e['event']=='present_begin' and e['a']:
            first_present.setdefault((e['a'],e['epoch']),e['qpc'])
    # Present packet epoch is the active RR epoch; reject accidental cross-epoch matching.
    for key,t in first_present.items():
        if key in snapshots and start<=t<end:
            pose, snap = snapshots[key]
            assert t>=pose
            durations['poseToFirstPresentMs'].append(ms(t-pose))
            if key in published:
                assert t>=published[key]
                durations['publishToFirstPresentMs'].append(ms(t-published[key]))
    results=[e for e in events if e['event']=='present_result' and measured(e)]
    if results:
        assert all(e['a']==0 for e in results), 'Present status/occlusion invalidates normal presentation measurement'
    report['presentStatusSamples']=len(results)
    report['sdkFailureCount']=sum(e['event']=='rr_sdk_end' and e['a']==0 and measured(e) for e in events)
    report['summaryFailureCount']=sum(e['event']=='rr_summary_result' and (not e['a'] or not e['b']) and measured(e) for e in events)
    admitted={(e['production'],e['epoch']):e['qpc'] for e in events if e['event']=='rr_admitted'}
    measured_presents=[e for e in events if e['event']=='present_end' and measured(e)]
    assert len(measured_presents)==trace['presents'], 'Present counter/window mismatch'
    fresh=[]; previous=0
    for e in events:
        if e['event']=='present_end':
            if e['a'] and e['a']!=previous and measured(e): fresh.append(e['qpc'])
            previous=e['a']
    assert len(fresh)==trace['freshPresents'], 'Fresh-present counter mismatch'
    durations['freshPresentIntervalMs']=[ms(b-a) for a,b in zip(fresh,fresh[1:])]
    durations['presentIntervalMs']=[ms(b['qpc']-a['qpc']) for a,b in zip(measured_presents,measured_presents[1:])]
    gpu=defaultdict(list)
    for p in trace['gpu']: gpu[p['submission']].append(p)
    cal = trace['calibrations']
    assert len(cal)>=2 and all(c['result']==0 and c['frequency']>0 for c in cal), 'Calibration failure'
    residual=[]
    for a,b in zip(cal,cal[1:]):
        assert a['frequency']==b['frequency'] and b['cpu']>=a['cpu']
        predicted=a['cpu']+(b['gpu']-a['gpu'])*freq/a['frequency']
        residual.append(ms(b['cpu']-predicted))
    report['calibration'] = dict(count=len(cal), adjacentResidualMs=stats(residual),
                                callBracketMs=stats([ms(c['after']-c['before']) for c in cal]))
    assoc={a['submission']:a for a in associations}
    packet_times={}
    for sub,points in gpu.items():
        assert sub in assoc and len(points)>=2
        ticks=[p['tick'] for p in points]
        assert all(b>=a for a,b in zip(ticks,ticks[1:])), 'GPU timestamp regressed'
        frequency=points[0]['frequency']
        assert frequency>0 and all(p['frequency']==frequency for p in points)
        times=[gpu_qpc(p['tick'],frequency,cal,freq) for p in points]
        packet_times[sub]=(times[0],times[-1])
        a=assoc[sub]
        if not start<=a['executeQpc']<end: continue
        durations['gpuPacketMs'].append(ms(times[-1]-times[0]))
        durations['gpuStartMinusExecuteEndMs'].append(ms(times[0]-a['executeEndQpc']))
        if a['registerQpc'] is not None:
            durations['rrRegisterMinusGpuPacketEndMs'].append(ms(a['registerQpc']-times[-1]))
        by_name={p['name']:t for p,t in zip(points,times)}
        a['gpuMarkersQpc']={n:t for n,t in by_name.items() if n.startswith('PF ') or n=='PT RR Input Admission'}
        for index,p in enumerate(points[1:],1):
            if p['name']=='PathTracingPass': durations['ptGpuMs'].append(ms(times[index]-times[index-1]))
        if 'PF RR SDK begin' in by_name and 'PF RR SDK end' in by_name:
            durations['rrSdkGpuMs'].append(ms(by_name['PF RR SDK end']-by_name['PF RR SDK begin']))
            durations['rrSubmitToGpuSdkBeginMs'].append(ms(by_name['PF RR SDK begin'] - a['executeEndQpc']))
            durations['rrValidationCopyGpuMs'].append(ms(by_name['PF RR summary copy end']-by_name['PF RR SDK end']))
        if 'publishedQpc' not in a:
            publication=published.get((a['production'],a['epoch']))
            # Only the output-submission owns SDK+summary completion, not the input-only packet.
            if publication is not None and 'PF RR summary copy end' in by_name:
                a['publishedQpc']=publication
                durations['summaryEndToPublicationMs'].append(ms(a['publishedQpc']-by_name['PF RR summary copy end']))
                a['summaryEndQpc']=by_name['PF RR summary copy end']
        a['gpuBeginQpc'],a['gpuEndQpc']=times[0],times[-1]
    inputs={(a['production'],a['epoch']):a for a in associations if a['rrPhase']==1}
    outputs={(a['production'],a['epoch']):a for a in associations if a['rrPhase']==3}
    for key,input_packet in inputs.items():
        if not start<=input_packet['executeQpc']<end: continue
        output_packet=outputs.get(key)
        if not output_packet or key not in admitted: continue
        input_end=input_packet.get('gpuMarkersQpc',{}).get('PT RR Input Admission')
        rr_begin=output_packet.get('gpuMarkersQpc',{}).get('PF RR SDK begin')
        if input_end is None or rr_begin is None: continue
        admission=admitted[key]
        durations['inputSummaryEndToAdmissionMs'].append(ms(admission-input_end))
        durations['admissionToRrGpuBeginMs'].append(ms(rr_begin-admission))
        durations['inputSummaryEndToRrGpuBeginMs'].append(ms(rr_begin-input_end))
    missing=[a['submission'] for a in associations if start<=a['executeQpc']<end and a['submission'] not in gpu]
    assert not missing, ('Missing measured GPU packets',missing)
    ordered=sorted(packet_times.items())
    for (sa,(_,end_a)),(sb,(begin_b,_)) in zip(ordered,ordered[1:]):
        if sb==sa+1 and start<=begin_b<end:
            durations['betweenAppGpuPacketsMs'].append(ms(begin_b-end_a))
    report['metrics']={n:stats(v) for n,v in durations.items()}
    report['associations']=associations
    report['measuredGpuPackets']=sum(start<=a['executeQpc']<end for a in associations)
    report['observations']=dict(events=len(events),gpuPoints=len(trace['gpu']),
                                snapshots=len(snapshots),published=len(published),firstPresents=len(first_present))
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    report=analyze(json.loads(args.trace.read_text(encoding='utf-8')))
    text=json.dumps(report,indent=2)
    if args.output: args.output.write_text(text,encoding='utf-8')
    else: print(text)


if __name__=='__main__': main()
