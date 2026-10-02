#!/usr/bin/env python3
"""Exercise live certification failures without claiming synthetic hosts are AI."""
import array
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest.mock import patch
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_stream


FAKE_HOST = r'''
import array, select, sys, time, wave
from pathlib import Path
args = sys.argv
duration = int(args[args.index('--duration') + 1])
path = Path(args[args.index('--record') + 1])
offline = '--offline' in args
coreaudio = '--silent-output' in args
if coreaudio and ('--no-audio' in args or offline):
    raise RuntimeError('CoreAudio must use the actual audio output')
with wave.open(str(path), 'wb') as output:
    output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
    output.writeframes(array.array('h', [1000, -1000]) * (duration * 48000))
if SCENARIO == 'truncated':
    path.write_bytes(path.read_bytes()[:-4])
backend = 'magenta-rt2-small (AI, CPU offline)' if offline else 'magenta-rt2-small (AI, Metal)'
sink = 'silent CoreAudio output' if coreaudio else 'offline render' if offline else 'silent real-time sink'
print('[buffer] requested_ms=160 actual_ms=160 actual_frames=7680', flush=True)
print('[MagentaRT] Combined Prompt (1) tokens: 1 2 3', flush=True)
print(f'Running: {backend} | 48 kHz stereo | {sink}', flush=True)
start = time.monotonic()
last_status = 0
updates = 0
scale = .3 if offline else 1.
while True:
    elapsed = time.monotonic() - start
    position = min(duration, elapsed / scale)
    frames = min(duration * 48000, int(position * 48000))
    if select.select([sys.stdin], [], [], .005)[0]:
        command = sys.stdin.readline().strip()
        if command and SCENARIO != 'ignored':
            updates += 1
            # Exercise both orderings: fast encoding can precede host acknowledgment.
            token = '[MagentaRT] Combined Prompt (1) tokens: 4 5 6'
            if updates == 1:
                print(token, flush=True)
            print('[control] ' + command, flush=True)
            if updates != 1:
                print(token, flush=True)
    if elapsed - last_status >= .04 or frames == duration * 48000:
        callback_metrics = ''
        if coreaudio:
            metrics = ('callbacks', 'timestamp_discontinuities', 'invalid_timestamps',
                       'callback_overruns', 'callback_errors', 'device_overloads')
            warmup = dict.fromkeys(metrics, 0)
            active = dict.fromkeys(metrics, 0)
            warmup['callbacks'] = 10
            active['callbacks'] = max(1, frames // 512)
            if SCENARIO in ('warmup_overload', 'active_overload'):
                warmup['device_overloads'] = 1
            if SCENARIO == 'active_overload':
                active['device_overloads'] = 1
            if SCENARIO == 'coreaudio_overrun':
                active['callback_overruns'] = 1
            total = {key: warmup[key] + active[key] for key in metrics}
            if SCENARIO == 'bad_phase_accounting':
                total['device_overloads'] += 1
            values = {'audio_active_started': 1, 'audio_overload_monitoring': 1}
            for prefix, counters in (('', total), ('warmup_', warmup), ('active_', active)):
                values.update({'audio_' + prefix + key: value for key, value in counters.items()})
                values['audio_' + prefix + 'max_callback_ms'] = .10
            callback_metrics = ' ' + ' '.join(f'{key}={value}' for key, value in values.items())
            if SCENARIO == 'missing_coreaudio_metrics':
                callback_metrics = ''
            elif SCENARIO == 'missing_phase_metrics':
                callback_metrics = ' ' + ' '.join(f'{key}={value}' for key, value in values.items()
                                                if not key.startswith(('audio_active_', 'audio_warmup_')))
        print(f'[status] seconds={frames / 48000:.2f} frames={frames} underruns=0 invalid_samples=0 '
              'recording_dropped=0 prompt_status=2 sink_deadline_misses=0 sink_max_late_ms=0.00'
              + callback_metrics, flush=True)
        last_status = elapsed
    if frames == duration * 48000:
        break
'''


def status(frame_count, omit=(), **changes):
    values = {"seconds": f"{frame_count / 48000:.2f}", "frames": frame_count, "underruns": 0, "invalid_samples": 0,
              "recording_dropped": 0, "prompt_status": 2, "sink_deadline_misses": 0, "sink_max_late_ms": "0.00",
              "audio_callbacks": max(1, frame_count // 512), "audio_timestamp_discontinuities": 0,
              "audio_invalid_timestamps": 0, "audio_callback_overruns": 0, "audio_callback_errors": 0,
              "audio_device_overloads": 0, "audio_overload_monitoring": 1, "audio_max_callback_ms": ".1"}
    values["audio_active_started"] = 1
    for key in check_stream.COREAUDIO_COUNTERS + ("max_callback_ms",):
        values["audio_active_" + key] = values["audio_" + key]
        values["audio_warmup_" + key] = 0
    values.update(changes)
    return "[status] " + " ".join(f"{key}={value}" for key, value in values.items() if key not in omit)


def completed_evidence(offline=False, acknowledge=True, encode=True, final_time=3., coreaudio=False, status_changes=None,
                       buffer_ms=160):
    report = {"commands": [], "statuses": []}
    evidence = check_stream.Evidence(report, 3, offline, coreaudio)
    backend = "magenta-rt2-small (AI, CPU offline)" if offline else "magenta-rt2-small (AI, Metal)"
    sink = "silent CoreAudio output" if coreaudio else "offline render" if offline else "silent real-time sink"
    def row(frames):
        return status(frames, **(status_changes or {}))
    if buffer_ms is not None:
        evidence.line(f"[buffer] requested_ms={buffer_ms} actual_ms={buffer_ms} actual_frames={buffer_ms * 48}", 0.)
    evidence.line(f"Running: {backend} | 48 kHz stereo | {sink}", 0.)
    evidence.line(row(0), 0.)
    for index, prompt in enumerate(("style one", "style two"), 1):
        report["commands"].append({"command": prompt, "elapsed_seconds": index,
                                   "audio_frames": index * 48000, "tokens_before": evidence.tokens})
        if acknowledge:
            evidence.line("[control] " + prompt, index + .01)
        if encode:
            evidence.line(check_stream.TOKEN_LINE + " 1 2 3", index + .02)
        evidence.line(row(index * 48000 + 4800), index + .1)
    evidence.line(row(144000), final_time)
    return evidence


class StreamEvidenceTests(unittest.TestCase):
    def test_coreaudio_requires_actual_callback_metrics_and_ignores_software_sink_metrics(self):
        completed_evidence(coreaudio=True, status_changes={"sink_deadline_misses": 5,
                                                          "sink_max_late_ms": "nan"}).finish(3, 10)
        required = ("audio_callbacks", "audio_timestamp_discontinuities", "audio_invalid_timestamps",
                    "audio_callback_overruns", "audio_callback_errors", "audio_device_overloads",
                    "audio_overload_monitoring", "audio_max_callback_ms", "audio_active_started")
        required += tuple("audio_" + phase + key for phase in ("warmup_", "active_")
                          for key in check_stream.COREAUDIO_COUNTERS + ("max_callback_ms",))
        for key in required:
            evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False, True)
            with self.subTest(missing=key), self.assertRaisesRegex(RuntimeError, key):
                evidence.line(status(48000, omit=(key,)), 1.)

    def test_coreaudio_error_counters_and_inactive_monitoring_fail(self):
        changes = [{"audio_active_callbacks": value} for value in (0, -.1, .5, "nan")]
        changes += [{"audio_active_" + key: 1} for key in check_stream.COREAUDIO_ERRORS]
        changes += [{"audio_overload_monitoring": 0}, {"audio_max_callback_ms": "nan"},
                    {"audio_max_callback_ms": "inf"}, {"audio_max_callback_ms": -1}, {"audio_active_started": 0}]
        for update in changes:
            evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False, True)
            with self.subTest(**update), self.assertRaises(RuntimeError):
                evidence.line(status(48000, **update), 1.)
        evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False, True)
        evidence.line(status(0, audio_callbacks=0, audio_active_callbacks=0), 0.)
        evidence.line(status(48000, audio_callbacks=10, audio_active_callbacks=10), 1.)
        with self.assertRaisesRegex(RuntimeError, "backwards"):
            evidence.line(status(96000, audio_callbacks=9, audio_active_callbacks=9), 2.)

    def test_coreaudio_preparation_is_retained_but_cannot_hide_active_errors(self):
        changes = {"audio_device_overloads": 1, "audio_warmup_device_overloads": 1}
        evidence = completed_evidence(coreaudio=True, status_changes=changes)
        evidence.finish(3, 10)
        self.assertEqual(evidence.report["coreaudio_phases"]["warmup"]["device_overloads"], 1)
        self.assertEqual(evidence.report["coreaudio_phases"]["active"]["device_overloads"], 0)
        with self.assertRaisesRegex(RuntimeError, "CoreAudio reported active"):
            completed_evidence(coreaudio=True, status_changes=changes | {"audio_active_device_overloads": 1,
                                                                        "audio_device_overloads": 2})
        for changes in ({"audio_device_overloads": 1}, {"audio_max_callback_ms": 1}):
            with self.subTest(**changes), self.assertRaisesRegex(RuntimeError, "phase"):
                completed_evidence(coreaudio=True, status_changes=changes).finish(3, 10)
        evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False, True)
        evidence.line(status(48000), 1.)
        with self.assertRaisesRegex(RuntimeError, "preparation counters changed"):
            evidence.line(status(96000, audio_warmup_device_overloads=1), 2.)

    def test_actual_buffer_contract_and_prompt_drain_window(self):
        completed_evidence().finish(3, 10)
        with self.assertRaisesRegex(RuntimeError, "drain the AI buffer"):
            completed_evidence(buffer_ms=1500).finish(3, 10)
        evidence = completed_evidence(buffer_ms=None)
        evidence.expected_buffer_ms = 1500
        with self.assertRaisesRegex(RuntimeError, "Missing actual AI buffer"):
            evidence.finish(3, 10)
        for line, expected in (("[buffer] requested_ms=160 actual_ms=160 actual_frames=7680", "expected buffer"),
                               ("[buffer] requested_ms=1500 actual_ms=1500 actual_frames=7680", "Inconsistent")):
            evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False, True, 1500)
            with self.subTest(line=line), self.assertRaisesRegex(RuntimeError, expected):
                evidence.line(line, 0.)

    def test_ready_status_requires_each_acknowledgment_and_new_encoding(self):
        completed_evidence().finish(3, 10)
        for kwargs in ({"acknowledge": False}, {"encode": False}):
            with self.subTest(**kwargs), self.assertRaisesRegex(RuntimeError, "prompt updates"):
                completed_evidence(**kwargs).finish(3, 10)

    def test_slow_sink_and_excessive_prompt_latency_fail(self):
        with self.assertRaisesRegex(RuntimeError, "elapsed playback"):
            completed_evidence(final_time=6).finish(3, 10)
        with self.assertRaisesRegex(RuntimeError, "latency"):
            completed_evidence().finish(3, .05)
        # Offline render is deliberately allowed to run slower than playback.
        completed_evidence(offline=True, final_time=30).finish(3, 60)

    def test_bad_backend_counters_and_stale_final_status_fail(self):
        for backend in ("procedural fake", "magenta-rt2-small (AI, CPU offline)"):
            evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False)
            with self.subTest(backend=backend), self.assertRaisesRegex(RuntimeError, "backend"):
                evidence.line(f"Running: {backend} | 48 kHz stereo | silent real-time sink", 0)
        for changes in ({"sink_deadline_misses": 1}, {"underruns": 1}, {"invalid_samples": 1},
                        {"recording_dropped": 1}, {"prompt_status": 3}, {"sink_max_late_ms": "nan"},
                        {"seconds": 9}, {"frames": -1}):
            evidence = check_stream.Evidence({"commands": [], "statuses": []}, 3, False)
            with self.subTest(**changes), self.assertRaises(RuntimeError):
                evidence.line(status(0, **changes), 0)
        evidence = completed_evidence()
        evidence.report["statuses"].pop()
        with self.assertRaisesRegex(RuntimeError, "Final status"):
            evidence.finish(3, 10)

    def test_prompt_finishing_only_after_audio_ends_does_not_pass(self):
        evidence = completed_evidence(encode=False)
        evidence.line(check_stream.TOKEN_LINE + " 1 2 3", 3.)
        evidence.line(status(144000), 3.)
        with self.assertRaisesRegex(RuntimeError, "prompt updates"):
            evidence.finish(3, 10)

    def test_pcm_format_header_and_actual_data_must_all_match(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "music.wav"
            def write(channels=2, rate=48000, width=2, value=1000):
                with wave.open(str(path), "wb") as output:
                    output.setparams((channels, width, rate, 0, "NONE", "not compressed"))
                    output.writeframes(array.array("h", [value]) * (48000 * channels))
            write()
            self.assertEqual(check_stream.audio_report(path, 48000)["frames"], 48000)
            for kwargs in ({"channels": 1}, {"rate": 8000}, {"width": 1}):
                write(**kwargs)
                with self.subTest(**kwargs), self.assertRaisesRegex(RuntimeError, "PCM16"):
                    check_stream.audio_report(path, 48000)
            write(value=0)
            with self.assertRaisesRegex(RuntimeError, "silent"):
                check_stream.audio_report(path, 48000)
            write()
            path.write_bytes(path.read_bytes()[:-4])
            with self.assertRaisesRegex(RuntimeError, "header"):
                check_stream.audio_report(path, 48000)


class StreamProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def host(self, scenario="good", body=FAKE_HOST):
        path = self.root / "fake-host"
        path.write_text(f"#!{sys.executable}\nSCENARIO = {scenario!r}\n" + textwrap.dedent(body))
        path.chmod(0o755)
        return path

    def run_validator(self, scenario="good", offline=False, optimized=False, body=FAKE_HOST, coreaudio=False):
        directory = self.root / "result"
        command = [sys.executable]
        if optimized:
            command.append("-O")
        command += [str(ROOT / "scripts/check_stream.py"), "--binary", str(self.host(scenario, body)),
                    "--duration", "3", "--directory", str(directory), "--style", "test initial style"]
        if offline:
            command.append("--offline")
        if coreaudio:
            command.append("--coreaudio")
        env = os.environ.copy()
        if optimized:
            env["PYTHONOPTIMIZE"] = "1"
        result = subprocess.run(command, capture_output=True, text=True, timeout=10, env=env)
        return result, json.loads((directory / "report.json").read_text())

    def test_good_fake_host_proves_orchestration_and_both_prompt_event_orders(self):
        result, report = self.run_validator()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["ok"])
        self.assertEqual(report["initial_style"], "test initial style")
        self.assertEqual(len(report["commands"]), 2)
        self.assertTrue(all(row["ready"] for row in report["commands"]))
        self.assertAlmostEqual(report["playback_wall_seconds"], 3, delta=.2)

    def test_coreaudio_host_mode_and_callback_contract(self):
        result, report = self.run_validator(coreaudio=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["ok"])
        self.assertEqual(report["mode"], "coreaudio")
        self.assertIn("silent CoreAudio output", report["backend"])
        self.assertGreater(int(report["statuses"][-1]["audio_callbacks"]), 0)
        self.assertGreater(report["audio"]["rms"], 1e-5)

    def test_coreaudio_missing_metrics_and_overruns_fail_under_optimization(self):
        for scenario, message in (("missing_coreaudio_metrics", "audio_callbacks"),
                                  ("missing_phase_metrics", "audio_warmup_callbacks"),
                                  ("coreaudio_overrun", "CoreAudio reported"),
                                  ("active_overload", "CoreAudio reported active"),
                                  ("bad_phase_accounting", "phase counters do not add up")):
            with self.subTest(scenario=scenario):
                result, report = self.run_validator(scenario, coreaudio=True, optimized=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(report["ok"])
                self.assertIn(message, report["error"])
                (self.root / "result").rename(self.root / scenario)

    def test_coreaudio_warmup_overload_is_explicit_in_success_report_under_optimization(self):
        result, report = self.run_validator("warmup_overload", coreaudio=True, optimized=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["ok"])
        self.assertEqual(report["coreaudio_phases"]["warmup"]["device_overloads"], 1)
        self.assertEqual(report["coreaudio_phases"]["active"]["device_overloads"], 0)
        self.assertIn("Silent preparation reported 1", result.stdout)
        self.assertEqual(report["buffer"]["actual_frames"], 7680)

    def test_coreaudio_and_offline_are_mutually_exclusive(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/check_stream.py"), "--coreaudio", "--offline"],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not allowed", result.stderr)

    def test_ignored_prompts_fail_even_with_optimized_python(self):
        result, report = self.run_validator("ignored", optimized=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report["ok"])
        self.assertIn("previous style prompt", report["error"])

    def test_audio_errors_cannot_be_disabled_by_python_optimization(self):
        body = '''
            print('Running: magenta-rt2-small (AI, Metal) | 48 kHz stereo | silent real-time sink', flush=True)
            print('[status] seconds=3.00 frames=144000 underruns=1 invalid_samples=0 recording_dropped=0 prompt_status=2 sink_deadline_misses=0 sink_max_late_ms=0.00', flush=True)
        '''
        result, report = self.run_validator(optimized=True, body=body)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(report["ok"])
        self.assertIn("audio gaps", report["error"])

    def test_offline_cpu_compatibility_and_wav_failure_report(self):
        result, report = self.run_validator(offline=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["ok"])
        (self.root / "result").rename(self.root / "offline-pass")
        result, report = self.run_validator("truncated", offline=True, optimized=True)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(report["ok"])
        self.assertIn("header", report["error"])
        self.assertEqual(report["mode"], "offline")
        self.assertTrue(all(row["ready"] for row in report["commands"]))

    def test_startup_failure_and_timeout_always_save_reports(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = check_stream.check(self.root / "missing", 3, self.root / "missing-result")
        report = json.loads((self.root / "missing-result/report.json").read_text())
        self.assertEqual(result, 1)
        self.assertFalse(report["ok"])
        self.assertIn("No such file", report["error"])
        host = self.host(body="import time\ntime.sleep(10)\n")
        with patch.object(check_stream.time, "monotonic", side_effect=[0., 400., 401.]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = check_stream.check(host, 3, self.root / "timeout-result")
        report = json.loads((self.root / "timeout-result/report.json").read_text())
        self.assertEqual(result, 1)
        self.assertFalse(report["ok"])
        self.assertIn("did not finish", report["error"])
        self.assertIsNotNone(report["returncode"])


if __name__ == "__main__":
    unittest.main()
