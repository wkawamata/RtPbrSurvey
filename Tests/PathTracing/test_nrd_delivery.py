from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from validate_nrd_delivery import run


class DeliveryProcessTests(unittest.TestCase):
    def execute(self, text, code=0, timeout=False):
        child = Mock()
        child.wait.side_effect = [subprocess.TimeoutExpired('owned-child', 1), 0] if timeout else None
        child.wait.return_value = code
        child.poll.return_value = None if timeout else code
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary)/'runtime.log'
            log.write_text(text, encoding='utf-8')
            with patch('validate_nrd_delivery.subprocess.STARTUPINFO',
                    return_value=SimpleNamespace(dwFlags=0), create=True), patch(
                    'validate_nrd_delivery.subprocess.STARTF_USESHOWWINDOW', 1, create=True), patch(
                    'validate_nrd_delivery.subprocess.Popen', return_value=child):
                if timeout:
                    with self.assertRaises(subprocess.TimeoutExpired):
                        run(['owned-child'], log, timeout=1)
                    child.kill.assert_called_once_with()
                    self.assertEqual(child.wait.call_count, 2)
                    return
                return run(['owned-child'], log)

    def test_successful_utf8_log_is_read_without_locale_dependence(self):
        self.assertEqual(self.execute('scene: \u767a\u5149\u9762\n'), ['scene: \u767a\u5149\u9762'])

    def test_runtime_and_debug_layer_failures_are_rejected(self):
        for text, code in (('', 1), ('[ERROR] device', 0), ('[CORRUPTION] invalid', 0),
                ('[PathTracingHistoryFailure] checkpoint', 0)):
            with self.subTest(text=text, code=code), self.assertRaises(ValueError):
                self.execute(text, code)

    def test_timeout_reaps_only_the_owned_child(self):
        self.execute('', timeout=True)


if __name__ == '__main__':
    unittest.main()
