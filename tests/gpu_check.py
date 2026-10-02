#!/usr/bin/env python3
"""Check diagnostic failure handling without requiring a GPU or model."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_gpu


def write_audio(path, frames=48000, value=1000):
    with wave.open(str(path), "wb") as output:
        output.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
        output.writeframes(struct.pack("<h", value) * (frames * 2))


class GpuCheckTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.root_patch = patch.object(check_gpu, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def run_check(self, runner):
        directory = self.root / "result"
        with patch.object(check_gpu, "run_logged", side_effect=runner) as calls:
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                result = check_gpu.check(directory, 1, False)
        return result, json.loads((directory / "report.json").read_text()), calls.call_count

    def test_failed_probe_never_starts_model(self):
        cases = [
            ({"ok": False, "error": "No GPU", "enumerated_devices": 0}, 2, False),
            ({"ok": False, "error": "Calculation failed", "enumerated_devices": 1}, 3, False),
            ({"ok": True, "device": "test"}, 0, True),
            ([], 0, False),
        ]
        for index, (metal, code, timed_out) in enumerate(cases):
            with self.subTest(metal=metal, timed_out=timed_out):
                def runner(command, log, timeout):
                    is_probe = log.name == "metal.log"
                    self.assertIn(log.name, ("compile.log", "metal.log"))
                    log.write_text(json.dumps(metal) if is_probe else "")
                    return {"returncode": code if is_probe else 0,
                            "timed_out": timed_out if is_probe else False, "wall_seconds": .1}
                result, report, calls = self.run_check(runner)
                self.assertEqual(result, 1)
                self.assertFalse(report["ok"])
                self.assertNotIn("generation", report)
                self.assertEqual(calls, 2)
                (self.root / "result").rename(self.root / f"failure-{index}")

    def test_successful_render_orchestration(self):
        # Synthetic tool results exercise validation, not actual GPU inference.
        binary = self.root / "build-ai/ai-music"
        binary.parent.mkdir()
        binary.touch()

        def runner(command, log, timeout):
            if log.name == "metal.log":
                log.write_text(json.dumps({"ok": True, "device": "synthetic test"}))
            elif log.name == "generation.log":
                self.assertEqual(command[0], str(binary))
                self.assertIn("--offline", command)
                write_audio(Path(command[command.index("--record") + 1]))
                log.write_text("Running: magenta-rt2-small (AI, Metal)\n"
                               "[status] underruns=0 invalid_samples=0 recording_dropped=0 prompt_status=2\n")
            else:
                log.write_text("")
            return {"returncode": 0, "timed_out": False, "wall_seconds": .1}

        result, report, calls = self.run_check(runner)
        self.assertEqual((result, calls), (0, 3))
        self.assertTrue(report["ok"])
        self.assertEqual(report["audio"]["frames"], 48000)

    def test_silent_incomplete_and_truncated_audio_fail(self):
        path = self.root / "audio.wav"
        write_audio(path, value=0)
        with self.assertRaisesRegex(RuntimeError, "silent"):
            check_gpu.audio_report(path, 48000)
        write_audio(path, frames=100)
        with self.assertRaisesRegex(RuntimeError, "Incomplete"):
            check_gpu.audio_report(path, 48000)
        write_audio(path)
        path.write_bytes(path.read_bytes()[:-4])
        with self.assertRaisesRegex(RuntimeError, "header"):
            check_gpu.audio_report(path, 48000)


if __name__ == "__main__":
    unittest.main()
