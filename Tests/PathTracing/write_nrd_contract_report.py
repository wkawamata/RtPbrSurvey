"""Generate a contract report from executed CPU and WARP corpus tests."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from test_nrd_gpu_contract_subset import gpu_inputs, run_gpu

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error('Output already exists; preserve prior evidence')
    corpus = ROOT/'Tests/PathTracing/nrd_pixel_contract_vectors.json'
    cpu = ROOT/'build/Debug/RtPbrSurvey.NrdRelaxAdapterTests.exe'
    gpu = ROOT/'build/Debug/RtPbrSurvey.PathTracingDenoiserSamplingTests.exe'
    shader = ROOT/'build/Tests/NrdPacking/shaders_NrdPixelContract_CSMain.cso'
    data = json.loads(corpus.read_text(encoding='utf-8'))
    report = dict(schemaVersion=2, status='failed', inputReady=False, runtimeGeometrySnapshotVerified=False,
        gpuProofClaimed=False, hostOnlyCategories=data['hostOnlyCategories'], records=[], failures=[],
        corpusSha256=digest(corpus), cpuExeSha256=digest(cpu), gpuExeSha256=digest(gpu),
        gpuShaderSha256=digest(shader), scriptSha256=digest(Path(__file__)),
        gpuHelperSha256=digest(ROOT/'Tests/PathTracing/test_nrd_gpu_contract_subset.py'), hardware=False)
    try:
        run = subprocess.run([str(cpu), str(corpus)], capture_output=True, text=True, timeout=120, cwd=ROOT)
        report['cpuStdoutSha256'] = hashlib.sha256(run.stdout.encode()).hexdigest()
        if run.returncode or 'executed=5 host-only=3' not in run.stdout:
            raise ValueError('CPU contract failed: '+run.stdout+run.stderr)
        for case in data['vectors']:
            if case['hostOnly']:
                continue
            marker = f"Pixel contract vector case executed: {case['id']} -> {case['expectedDisposition']}"
            if run.stdout.splitlines().count(marker) != 1:
                raise ValueError('Incomplete CPU case evidence: '+case['id'])
            report['records'].append(dict(id='cpu:'+case['id'], caseId=case['id'], domain='cpu', passed=True,
                disposition=case['expectedDisposition'], d3d12Errors=0))
        for case_id in run_gpu(data, gpu, shader):
            report['records'].append(dict(id='gpu:'+case_id, caseId=case_id, domain='gpu', passed=True,
                d3d12Errors=0))
        report['status'] = 'passed'
    except Exception as error:
        report['failures'].append(str(error))
    report['limitation'] = ('CPU suite and WARP signal arithmetic/packing only. Host provenance is CPU-only; '
        'no live runtime readiness or RELAX denoising is certified.')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes((json.dumps(report, indent=2)+'\n').replace('\n', '\r\n').encode('utf-8'))
    print(report['status'], 'records='+str(len(report['records'])), report['failures'])
    return report['status'] != 'passed'


if __name__ == '__main__':
    raise SystemExit(main())
