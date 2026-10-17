import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'Tests/PathTracing/Start-NrdVisualReview.ps1'
SHELL = shutil.which('pwsh') or shutil.which('powershell')


@unittest.skipUnless(SHELL, 'PowerShell is required')
class VisualReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.executable = self.directory / 'fake app.exe'
        self.executable.write_bytes(b'not an executable')
        self.output = self.directory / 'review output'

    def run_script(self, *arguments):
        return subprocess.run([SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(SCRIPT), '-ExecutablePath', str(self.executable),
            '-OutputDirectory', str(self.output), *arguments],
            capture_output=True, encoding='utf-8', errors='replace')

    def test_dry_run_never_writes_or_launches(self):
        for scene in ('Disocclusion', 'StableRadiance'):
            result = self.run_script('-DryRun', '-Scene', scene, '-InitialMode', 'Native')
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads(result.stdout)
            self.assertEqual(manifest['status'], 'dry-run')
            self.assertFalse(manifest['preset']['pathTracing']['nrdPreview'])
            self.assertIn('-NrdContinuousCameraHistory', manifest['arguments'])
            self.assertNotIn('-ExitAfterCapture', manifest['arguments'])
            self.assertNotIn('-CapturePath', manifest['arguments'])
            self.assertFalse(self.output.exists())

    def test_prepare_retains_unanswered_results_and_source(self):
        source = ROOT / 'Assets/Scene/PathTracingValidation/14-nrd-disocclusion/render-preset.json'
        before = hashlib.sha256(source.read_bytes()).digest()
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((self.output / 'session.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['status'], 'prepared')
        self.assertNotIn('processId', manifest)
        self.assertEqual(manifest['subjectiveVerdict'], 'unanswered')
        results = json.loads((self.output / 'results.json').read_text(encoding='utf-8'))
        self.assertEqual(len(results['cases']), 5)
        self.assertTrue(all(case['result'] == 'unanswered' for case in results['cases']))
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
        for path in self.output.glob('*.json'):
            data = path.read_bytes()
            self.assertFalse(data.startswith(b'\xef\xbb\xbf'))
            self.assertNotIn(b'\n', data.replace(b'\r\n', b''))
        self.assertFalse((self.output / 'runtime.log').exists())

    def test_stable_radiance_filters_cases(self):
        result = self.run_script('-Scene', 'StableRadiance')
        self.assertEqual(result.returncode, 0, result.stderr)
        results = json.loads((self.output / 'results.json').read_text(encoding='utf-8'))
        self.assertEqual([case['id'] for case in results['cases']], ['NRD-LIVE-06'])

    def test_nonempty_output_is_not_overwritten(self):
        self.output.mkdir()
        marker = self.output / 'keep.txt'
        marker.write_text('keep', encoding='utf-8')
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertEqual(marker.read_text(encoding='utf-8'), 'keep')
        self.assertEqual(list(self.output.iterdir()), [marker])

    def test_dry_run_launch_conflict_is_rejected(self):
        self.assertNotEqual(self.run_script('-DryRun', '-Launch').returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
