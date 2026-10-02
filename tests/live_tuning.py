#!/usr/bin/env python3
"""Reject misleading timing data before allowing a live playback test."""
import json
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from tune_live import benchmark_report, best_candidate, live_report
import tune_live


class LiveTuningTests(unittest.TestCase):
    def test_environment_clears_inherited_experiments_before_applying_preset(self):
        inherited = {key: "inherited experiment" for key in tune_live.INFERENCE_ENV_KEYS}
        with patch.dict(tune_live.os.environ, inherited | {"PATH": "/test/bin"}, clear=True):
            settings = {"AI_MUSIC_CACHE_CONSTANTS": "1"}
            self.assertEqual(tune_live.environment(settings), {"PATH": "/test/bin", **settings})

    def test_incomplete_nonfinite_or_inconsistent_benchmark_is_rejected(self):
        valid = {"ok": True, "steps": 100, "mean_ms": 20., "p95_ms": 25.,
                 "measured_seconds": 2., "realtime_factor": .5, "rms": .02}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "benchmark.json"
            path.write_text(json.dumps(valid))
            self.assertEqual(benchmark_report(path, 100), valid)
            for changes in ({"ok": False}, {"steps": 99}, {"mean_ms": float("nan")},
                            {"p95_ms": float("inf")}, {"realtime_factor": .1}, {"rms": 0}):
                with self.subTest(changes=changes):
                    path.write_text(json.dumps(valid | changes))
                    with self.assertRaises(RuntimeError):
                        benchmark_report(path, 100)

    def test_slow_generation_never_qualifies_for_live_playback(self):
        slow = {"name": "slow", "metrics": {"realtime_factor": 1.01}}
        edge = {"name": "no headroom", "metrics": {"realtime_factor": 1.}}
        failed = {"name": "missing results"}
        self.assertIsNone(best_candidate([slow, edge, failed]))
        fast = {"name": "fast", "metrics": {"realtime_factor": .6}}
        self.assertEqual(best_candidate([slow, edge, failed, fast]), fast)

    def test_live_gate_requires_explicit_success_and_same_tested_style(self):
        valid = {"ok": True, "mode": "coreaudio", "returncode": 0,
                 "requested_seconds": 120, "initial_style": "funk", "buffer": {"actual_frames": 240000},
                 "coreaudio_phases": {"warmup": {}, "active": {}}}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.json"
            path.write_text(json.dumps(valid))
            self.assertEqual(live_report(path, 120, "funk"), valid)
            for changes in ({"ok": False}, {"ok": None}, {"mode": "offline"}, {"mode": "real_time"}, {"returncode": 1},
                            {"requested_seconds": 60}, {"initial_style": "different prompt"},
                            {"buffer": {"actual_frames": 96000}}, {"buffer": {}}, {"coreaudio_phases": {}}):
                with self.subTest(changes=changes):
                    path.write_text(json.dumps(valid | changes))
                    with self.assertRaises(RuntimeError):
                        live_report(path, 120, "funk")

    def test_playback_gate_requests_coreaudio_and_rejects_software_sink_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build-ai").mkdir()
            for name in ("ai-music", "music-benchmark"):
                (root / "build-ai" / name).touch()

            def runner(command, log, timeout, env=None):
                log.write_text("")
                if "music-benchmark" in command[0]:
                    Path(command[-1]).with_suffix(".json").write_text(json.dumps({
                        "ok": True, "steps": 100, "mean_ms": 20., "p95_ms": 25.,
                        "measured_seconds": 2., "realtime_factor": .5, "rms": .02,
                    }))
                elif any("check_stream.py" in item for item in command):
                    self.assertIn("--coreaudio", command)
                    self.assertEqual(command[command.index("--expected-buffer-ms") + 1], "5000")
                    self.assertEqual(command[command.index("--style") + 1], tune_live.PROMPT)
                    live = Path(command[command.index("--directory") + 1])
                    live.mkdir()
                    (live / "report.json").write_text(json.dumps({
                        "ok": True, "mode": "real_time", "returncode": 0,
                        "requested_seconds": 60, "initial_style": tune_live.PROMPT,
                    }))
                return {"returncode": 0, "timed_out": False}

            with patch.object(tune_live, "ROOT", root), patch.object(tune_live, "run_logged", side_effect=runner), \
                    patch.object(tune_live.subprocess, "Popen") as playback, \
                    patch.object(sys, "argv", ["tune_live.py", "--play", "--steps", "100", "--duration", "60"]), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                result = tune_live.main()
            self.assertEqual(result, 1)
            playback.assert_not_called()
            report = json.loads(next((root / "validation").glob("*/report.json")).read_text())
            self.assertFalse(report["ok"])
            self.assertIn("CoreAudio", report["error"])


if __name__ == "__main__":
    unittest.main()
