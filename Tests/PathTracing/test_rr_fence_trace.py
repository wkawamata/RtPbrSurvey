"""CPU-only tests of trace identity, ordering and clock interpretation."""
import copy
import unittest
from analyze_rr_fence_trace import analyze, gpu_qpc


def fixture():
    rows=[]
    def e(t,name,frame=1,sub=1,a=0,b=0,c=0,d=0):
        rows.append(dict(qpc=t,event=name,frame=frame,tick=frame,submission=sub,
                         production=1,epoch=1,a=a,b=b,c=c,d=d))
    e(102,'pose_ready');e(105,'input_snapshot',a=1,b=1)
    for frame,base,sub,rr,fence in [(1,110,1,0,1),(2,210,2,1,2)]:
        for offset,name in [(0,'execute_begin'),(1,'execute_end'),(2,'present_begin'),
                            (3,'present_end'),(4,'signal_begin'),(5,'signal_end')]:
            e(base+offset,name,frame,sub,a=rr if 'present' in name else fence if 'signal' in name else 0)
    e(116,'resource_wait_begin',a=0,c=0)
    e(140,'resource_wait_end',a=0,c=1)
    e(141,'rr_mark_submitted',a=1)
    e(200,'rr_poll',2,0,a=1,b=1,c=3)
    e(201,'rr_summary',2,0)
    e(202,'published',2,0,a=1)
    rows.sort(key=lambda r:r['qpc'])
    gpu=[]
    for sub,points in [(1,[(112,'StartGpu'),(114,'PF RR SDK begin'),(130,'PF RR SDK end'),
                           (131,'PF RR summary copy end'),(132,'EndGpu')]),
                       (2,[(214,'StartGpu'),(220,'EndGpu')])]:
        for tick,name in points: gpu.append(dict(submission=sub,tick=tick,frequency=1000,node=0,index=-1,name=name))
    return dict(finished=True,overflow=False,qpcFrequency=1000,windowStart=100,windowEnd=400,
                presents=2,freshPresents=1,published=1,metadata={},timingOnly=False,
                events=rows,gpu=gpu,calibrations=[dict(before=t,after=t+1,gpu=t,cpu=t,frequency=1000,result=0)
                                                for t in [0,500]])


class TraceTests(unittest.TestCase):
    def test_identity_and_distinct_latency_components(self):
        r=analyze(fixture());m=r['metrics']
        self.assertAlmostEqual(r['presentFps'],2/.3)
        self.assertAlmostEqual(r['freshPresentFps'],1/.3)
        self.assertEqual(m['signalToRrRegisterMs']['mean'],26)
        self.assertEqual(m['rrSdkGpuMs']['mean'],16)
        self.assertEqual(m['summaryEndToPublicationMs']['mean'],71)
        self.assertEqual(m['poseToFirstPresentMs']['mean'],110)
        self.assertEqual(m['publishToFirstPresentMs']['mean'],10)
        self.assertEqual(m['rrRegisterMinusGpuPacketEndMs']['mean'],9)

    def test_regression_and_wrong_fence_rejected(self):
        for name in ['execute_end','rr_mark_submitted']:
            f=fixture();row=next(e for e in f['events'] if e['event']==name)
            if name=='execute_end': row['qpc']=100
            else: row['a']=99
            with self.assertRaises(AssertionError): analyze(f)

    def test_missing_gpu_and_counter_mismatch_rejected(self):
        f=fixture();f['gpu']=[x for x in f['gpu'] if x['submission']!=1]
        with self.assertRaises(AssertionError): analyze(f)
        f=fixture();f['freshPresents']=2
        with self.assertRaises(AssertionError): analyze(f)

    def test_failed_or_wrong_frequency_calibration_rejected(self):
        f=fixture();f['calibrations'][0]['result']=-1
        with self.assertRaises(AssertionError): analyze(f)
        with self.assertRaises(ValueError): gpu_qpc(10,2000,fixture()['calibrations'],1000)

    def test_occluded_present_rejected(self):
        f=fixture()
        row=dict(f['events'][0]);row.update(event='present_result',qpc=112,a=0x087A0001)
        f['events'].append(row);f['events'].sort(key=lambda e:e['qpc'])
        with self.assertRaises(AssertionError): analyze(f)

    def test_overflow_rejected(self):
        f=fixture();f['overflow']=True
        with self.assertRaises(AssertionError): analyze(f)

    def test_timing_control_has_no_events(self):
        f=fixture();f.update(timingOnly=True,events=[],gpu=[],calibrations=[])
        r=analyze(f);self.assertAlmostEqual(r['publishedFps'],1/.3)
        f['events']=[{}]
        with self.assertRaises(AssertionError): analyze(f)


if __name__=='__main__': unittest.main()
