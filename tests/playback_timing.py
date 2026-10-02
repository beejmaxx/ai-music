#!/usr/bin/env python3
"""Exercise the real silent sink, including a process stall with no underruns."""
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build-ai/ai-music").resolve()


class PlaybackTimingTests(unittest.TestCase):
    def run_host(self, pause=False):
        with tempfile.TemporaryDirectory(prefix="ai-music-deadlines-") as directory:
            log = Path(directory) / "session.log"
            with log.open("w") as output:
                process = subprocess.Popen([
                    str(BINARY), "--source", "demo", "--no-audio", "--duration", "2",
                    "--stats-every", ".1",
                ], stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, text=True)
                try:
                    deadline = time.monotonic() + 5
                    while "Running:" not in log.read_text():
                        if process.poll() is not None or time.monotonic() > deadline:
                            self.fail(log.read_text())
                        time.sleep(.01)
                    process.stdin.write("style pulse\nstyle invalid-style\n")
                    process.stdin.flush()
                    if pause:
                        # Freeze our own host, then let it catch up. A demo
                        # source is always ready, so ring underruns stay zero.
                        process.send_signal(signal.SIGSTOP)
                        time.sleep(.1)
                        process.send_signal(signal.SIGCONT)
                    process.wait(timeout=6)
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGCONT)
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    process.stdin.close()
            return process.returncode, log.read_text()

    def test_healthy_sink_acknowledges_only_accepted_styles(self):
        code, log = self.run_host()
        self.assertEqual(code, 0, log)
        self.assertIn("[control] style pulse\n", log)
        self.assertNotIn("[control] style invalid-style", log)
        self.assertIn("[command]", log)
        self.assertRegex(log, r"sink_deadline_misses=0 sink_max_late_ms=0.00")

    def test_stalled_sink_fails_even_without_source_underruns(self):
        code, log = self.run_host(pause=True)
        self.assertEqual(code, 2, log)
        statuses = [dict(re.findall(r"([a-z_]+)=([0-9.]+)", line))
                    for line in log.splitlines() if line.startswith("[status]")]
        self.assertTrue(statuses, log)
        final = statuses[-1]
        self.assertEqual(int(final["frames"]), 2 * 48000)
        self.assertEqual(int(final["underruns"]), 0)
        self.assertGreater(int(final["sink_deadline_misses"]), 0)
        self.assertGreater(float(final["sink_max_late_ms"]), 30)


if __name__ == "__main__":
    unittest.main()
