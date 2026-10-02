#!/usr/bin/env python3
"""Record an AI session and verify timing, encoded prompt updates, and audio."""
import argparse
import array
import datetime
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import wave


DEFAULT_STYLE = "instrumental electronic funk and techno, driving drums, gritty synth bass, evolving synth riff"
ERROR_COUNTERS = ("underruns", "invalid_samples", "recording_dropped")
COREAUDIO_ERRORS = ("timestamp_discontinuities", "invalid_timestamps", "callback_overruns",
                    "callback_errors", "device_overloads")
COREAUDIO_COUNTERS = ("callbacks",) + COREAUDIO_ERRORS
INFERENCE_ENV_KEYS = ("AI_MUSIC_BUFFER_MS", "AI_MUSIC_CACHE_CONSTANTS", "AI_MUSIC_WIRED_MB",
                      "AI_MUSIC_GPU_KEEPALIVE", "AI_MUSIC_FRAME_TRACE", "MLX_MAX_OPS_PER_BUFFER",
                      "MLX_MAX_MB_PER_BUFFER", "MLX_DISABLE_COMPILE", "MLX_METAL_FAST_SYNCH", "MLX_BFS_MAX_WIDTH")
TOKEN_LINE = "[MagentaRT] Combined Prompt (1) tokens:"


def number(row, key):
    try:
        value = float(row[key])
    except (KeyError, TypeError, ValueError):
        raise RuntimeError(f"Missing or invalid status metric: {key}") from None
    if not math.isfinite(value) or value < 0:
        raise RuntimeError(f"Missing or invalid status metric: {key}")
    return value


def inference_settings(environment):
    return {key: environment[key] for key in INFERENCE_ENV_KEYS if key in environment}


def audio_report(path, expected_frames):
    with wave.open(str(path), "rb") as recording:
        if (recording.getframerate(), recording.getnchannels(), recording.getsampwidth()) != (48000, 2, 2):
            raise RuntimeError("The recording is not 48 kHz stereo PCM16")
        frames = recording.getnframes()
        if frames != expected_frames:
            raise RuntimeError(f"Incomplete recording: expected {expected_frames} frames, got {frames}")
        energy, peak, count = 0, 0, 0
        while block := recording.readframes(65536):
            samples = array.array("h", block)
            if sys.byteorder != "little":
                samples.byteswap()
            count += len(samples)
            energy += sum(value * value for value in samples)
            peak = max(peak, abs(min(samples)), abs(max(samples)))
    if count != expected_frames * 2:
        raise RuntimeError("The WAV header describes more audio than the file contains")
    rms = math.sqrt(energy / max(count, 1)) / 32768
    if rms <= 1e-5:
        raise RuntimeError("The recording is effectively silent")
    return {"frames": frames, "sample_rate": 48000, "channels": 2, "sample_width": 2,
            "rms": rms, "peak": peak / 32768}


class Evidence:
    """Consume complete log lines once, preserving ordering across prompt events."""

    def __init__(self, report, duration, offline, coreaudio=False, expected_buffer_ms=None):
        self.report = report
        self.expected_frames = duration * 48000
        self.offline = offline
        self.coreaudio = coreaudio
        self.expected_buffer_ms = expected_buffer_ms
        self.started = None
        self.finished = None
        self.tokens = 0
        self.pending = ""

    def feed(self, text, now):
        self.pending += text
        lines = self.pending.split("\n")
        self.pending = lines.pop()
        for line in lines:
            self.line(line.rstrip("\r"), now)

    def line(self, line, now):
        if line.startswith("[buffer]"):
            if "buffer" in self.report or self.started is not None:
                raise RuntimeError("Unexpected buffer configuration change during the stream")
            values = dict(re.findall(r"([a-z_]+)=(\S+)", line))
            buffer = {key: number(values, key) for key in ("requested_ms", "actual_ms", "actual_frames")}
            if (any(value != int(value) for value in buffer.values())
                    or not 120 <= buffer["requested_ms"] <= 5000
                    or buffer["actual_frames"] != buffer["actual_ms"] * 48
                    or buffer["actual_ms"] != buffer["requested_ms"]):
                raise RuntimeError("Inconsistent AI buffer configuration")
            self.report["buffer"] = {key: int(value) for key, value in buffer.items()}
            if self.expected_buffer_ms is not None and buffer["actual_ms"] != self.expected_buffer_ms:
                raise RuntimeError("The actual AI buffer does not match the expected buffer size")
        if line.startswith("Running:"):
            allowed = ("magenta-rt2-small (AI, Metal)", "magenta-rt2-small (AI, CPU offline)") if self.offline else ("magenta-rt2-small (AI, Metal)",)
            sink = "silent CoreAudio output" if self.coreaudio else "offline render" if self.offline else "silent real-time sink"
            if self.started is not None or line not in {f"Running: {backend} | 48 kHz stereo | {sink}" for backend in allowed}:
                raise RuntimeError("The stream did not identify the expected AI backend and sink")
            if self.expected_buffer_ms is not None and "buffer" not in self.report:
                raise RuntimeError("Missing actual AI buffer configuration")
            self.started = now
            self.report["backend"] = line
        command = self.report["commands"][-1] if self.report["commands"] else None
        elapsed = now - self.started if self.started is not None else 0
        if line.startswith(TOKEN_LINE):
            self.tokens += 1
            if command and not command.get("ready") and self.tokens > command["tokens_before"]:
                command["encoded"] = True
                command["encoded_elapsed_seconds"] = round(elapsed, 3)
        if command and line == "[control] " + command["command"]:
            command["accepted"] = True
            command["accepted_elapsed_seconds"] = round(elapsed, 3)
        if not line.startswith("[status]"):
            return
        row = dict(re.findall(r"([a-z_]+)=(\S+)", line))
        self.report["statuses"].append(row)
        frames = number(row, "frames")
        seconds = number(row, "seconds")
        if frames != int(frames) or frames > self.expected_frames or abs(seconds - frames / 48000) > .011:
            raise RuntimeError("Status audio position is inconsistent")
        if len(self.report["statuses"]) > 1 and frames < number(self.report["statuses"][-2], "frames"):
            raise RuntimeError("Status audio position moved backwards")
        if any(number(row, key) != 0 for key in ERROR_COUNTERS):
            raise RuntimeError("The stream reported audio gaps or recording errors")
        prompt_status = number(row, "prompt_status")
        if prompt_status not in (0, 1, 2):
            raise RuntimeError("A style prompt failed encoding")
        if self.coreaudio:
            self.coreaudio_status(row, frames)
        elif not self.offline:
            if number(row, "sink_deadline_misses") != 0:
                raise RuntimeError("The real-time sink missed an audio deadline")
            number(row, "sink_max_late_ms")
        if frames == self.expected_frames and self.finished is None:
            self.finished = now
        if (command and not command.get("ready") and command.get("accepted") and command.get("encoded")
                and prompt_status == 2 and command["audio_frames"] < frames < self.expected_frames):
            command["ready"] = True
            command["ready_elapsed_seconds"] = round(elapsed, 3)
            command["ready_audio_seconds"] = frames / 48000
            command["latency_seconds"] = round(elapsed - command["elapsed_seconds"], 3)

    def coreaudio_status(self, row, frames):
        previous = self.report["statuses"][-2] if len(self.report["statuses"]) > 1 else None
        for phase in ("", "warmup_", "active_"):
            for metric in COREAUDIO_COUNTERS:
                key = "audio_" + phase + metric
                value = number(row, key)
                if value != int(value):
                    raise RuntimeError(f"Invalid CoreAudio counter: {key}")
                if previous and value < number(previous, key):
                    raise RuntimeError(f"CoreAudio counter moved backwards: {key}")
                if previous and phase == "warmup_" and value != number(previous, key):
                    raise RuntimeError("CoreAudio preparation counters changed during active playback")
            key = "audio_" + phase + "max_callback_ms"
            value = number(row, key)
            if previous and value < number(previous, key):
                raise RuntimeError(f"CoreAudio callback maximum moved backwards: {key}")
            if previous and phase == "warmup_" and value != number(previous, key):
                raise RuntimeError("CoreAudio preparation timing changed during active playback")
        self.coreaudio_accounting(row)
        if number(row, "audio_active_started") != 1:
            raise RuntimeError("CoreAudio has not entered active model playback")
        if frames > 0 and number(row, "audio_active_callbacks") == 0:
            raise RuntimeError("Recorded audio lacks active CoreAudio callback evidence")
        if any(number(row, "audio_active_" + key) != 0 for key in COREAUDIO_ERRORS):
            raise RuntimeError("CoreAudio reported active callback errors, discontinuities, or device overloads")
        if number(row, "audio_overload_monitoring") != 1:
            raise RuntimeError("CoreAudio device overload monitoring is not active")

    def coreaudio_accounting(self, row):
        phases = {}
        for phase in ("warmup", "active"):
            phases[phase] = {key: int(number(row, f"audio_{phase}_{key}")) for key in COREAUDIO_COUNTERS}
            phases[phase]["max_callback_ms"] = number(row, f"audio_{phase}_max_callback_ms")
        self.report["coreaudio_phases"] = phases
        self.report["coreaudio_total"] = {key: int(number(row, "audio_" + key)) for key in COREAUDIO_COUNTERS}
        self.report["coreaudio_total"]["max_callback_ms"] = number(row, "audio_max_callback_ms")
        # The host derives cumulative totals from the same phase snapshot, so
        # these equalities must hold even while callbacks are running.
        for key in COREAUDIO_COUNTERS:
            if number(row, "audio_" + key) != phases["warmup"][key] + phases["active"][key]:
                raise RuntimeError(f"CoreAudio phase counters do not add up: {key}")
        if number(row, "audio_max_callback_ms") != max(phase["max_callback_ms"] for phase in phases.values()):
            raise RuntimeError("CoreAudio phase callback timing does not match the total")

    def finish(self, duration, prompt_timeout):
        statuses = self.report["statuses"]
        commands = self.report["commands"]
        if self.started is None or not statuses:
            raise RuntimeError("Missing stream startup or status evidence")
        if self.expected_buffer_ms is not None and "buffer" not in self.report:
            raise RuntimeError("Missing actual AI buffer configuration")
        if number(statuses[-1], "frames") != self.expected_frames or self.finished is None:
            raise RuntimeError("Final status does not cover the complete recording")
        if number(statuses[-1], "prompt_status") != 2:
            raise RuntimeError("The final style prompt did not finish encoding")
        if len(commands) != 2 or not all(command.get("ready") for command in commands):
            raise RuntimeError("Missing accepted and encoded prompt updates during playback")
        if any(command["latency_seconds"] > prompt_timeout for command in commands):
            raise RuntimeError("A style prompt exceeded the encoding latency limit")
        if "buffer" in self.report:
            # Encoding readiness is not audible-response latency. Leave enough
            # playback for the queued audio and one in-flight 40 ms model frame
            # before replacing each prompt or ending the recording.
            drain_frames = self.report["buffer"]["actual_frames"] + 1920
            for index, command in enumerate(commands):
                end = commands[index + 1]["audio_frames"] if index + 1 < len(commands) else self.expected_frames
                if end - round(command["ready_audio_seconds"] * 48000) < drain_frames:
                    raise RuntimeError("Insufficient playback after an encoded prompt to drain the AI buffer")
        elapsed = self.finished - self.started
        self.report["playback_wall_seconds"] = round(elapsed, 3)
        if not self.offline:
            tolerance = max(.5, min(2., duration * .01))
            self.report["playback_tolerance_seconds"] = tolerance
            if abs(elapsed - duration) > tolerance:
                raise RuntimeError("Recorded audio duration does not match real-time elapsed playback")


def check(binary, duration, root, offline=False, style=DEFAULT_STYLE, prompt_timeout=None, coreaudio=False,
          expected_buffer_ms=None):
    root.mkdir(parents=True, exist_ok=False)
    log = root / "session.log"
    audio = root / "music.wav"
    prompt_timeout = prompt_timeout if prompt_timeout is not None else (60 if offline else 10)
    report = {"ok": False, "mode": "coreaudio" if coreaudio else "offline" if offline else "real_time", "requested_seconds": duration,
              "initial_style": style, "prompt_timeout_seconds": prompt_timeout,
              "inference_environment": inference_settings(os.environ), "expected_buffer_ms": expected_buffer_ms,
              "commands": [], "statuses": [], "returncode": None}
    evidence = Evidence(report, duration, offline, coreaudio, expected_buffer_ms)
    changes = [
        (duration / 3, "style instrumental jazz trio, gentle piano, upright bass, brushed drums"),
        (2 * duration / 3, "style instrumental downtempo electronic, warm synthesizers, gentle percussion"),
    ]
    process = None
    wall_start = time.monotonic()
    deadline = wall_start + duration * (20 if offline else 1) + 300
    try:
        if offline and coreaudio:
            raise RuntimeError("CoreAudio playback and offline rendering are mutually exclusive")
        with log.open("w") as output, log.open() as incoming:
            command = [str(binary.resolve()), "--source", "magenta", "--style", style,
                       "--duration", str(duration), "--record", str(audio), "--stats-every", ".25"]
            command.append("--silent-output" if coreaudio else "--no-audio")
            if offline:
                command.append("--offline")
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, text=True)
            try:
                while True:
                    now = time.monotonic()
                    was_started = evidence.started is not None
                    evidence.feed(incoming.read(), now)
                    if not was_started and evidence.started is not None:
                        print(f"Rendering {duration}s ({report['mode']}); recording in {root}", flush=True)
                    if process.poll() is not None:
                        # The child may have written its final status between the read and poll.
                        evidence.feed(incoming.read(), time.monotonic())
                        break
                    if now > deadline:
                        raise TimeoutError(f"Stream did not finish; see {log}")
                    elapsed = now - evidence.started if evidence.started is not None else 0
                    sent = report["commands"]
                    if sent and not sent[-1].get("ready") and elapsed - sent[-1]["elapsed_seconds"] > prompt_timeout:
                        raise RuntimeError("A style prompt was not accepted and encoded within the latency limit")
                    position = number(report["statuses"][-1], "frames") if report["statuses"] else 0
                    control_time = position / 48000 if offline else elapsed
                    if evidence.started is not None and changes and control_time >= changes[0][0]:
                        if sent and not sent[-1].get("ready"):
                            raise RuntimeError("The previous style prompt was not encoded before the next scheduled update")
                        _, command = changes.pop(0)
                        process.stdin.write(command + "\n")
                        process.stdin.flush()
                        sent.append({"elapsed_seconds": round(elapsed, 6), "audio_seconds": position / 48000,
                                     "audio_frames": int(position), "command": command, "tokens_before": evidence.tokens})
                        print(f"Sent {command}", flush=True)
                    time.sleep(.05)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                process.stdin.close()
        report["returncode"] = process.returncode
        if process.returncode != 0:
            raise RuntimeError(f"Stream failed with exit code {process.returncode}; see {log}")
        evidence.finish(duration, prompt_timeout)
        report["audio"] = audio_report(audio, duration * 48000)
        report["ok"] = True
        mode = "real-time AI generation through silent CoreAudio output" if coreaudio else "offline AI render" if offline else "real-time AI generation into a silent sink"
        print(f"PASS: {mode}, two accepted and encoded prompt updates, complete WAV, no counted playback gaps.", flush=True)
        if coreaudio:
            preparation_errors = sum(report["coreaudio_phases"]["warmup"][key] for key in COREAUDIO_ERRORS)
            if preparation_errors:
                print(f"Silent preparation reported {preparation_errors} audio error notifications; retained in coreaudio_phases.warmup.", flush=True)
    except (OSError, ValueError, RuntimeError, EOFError, wave.Error) as error:
        report["error"] = str(error)
        print(str(error), file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        report["error"] = "Interrupted by user"
    finally:
        if process is not None:
            report["returncode"] = process.returncode
        if evidence.started is not None and evidence.finished is not None:
            report["playback_wall_seconds"] = round(evidence.finished - evidence.started, 3)
        report["wall_seconds"] = round(time.monotonic() - wall_start, 3)
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"Evidence: {root / 'report.json'}", flush=True)
    return 0 if report["ok"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build-ai/ai-music"))
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--style", default=DEFAULT_STYLE, help="Initial AI style prompt")
    parser.add_argument("--expected-buffer-ms", type=int, help="Require this actual buffer size in the host startup evidence")
    parser.add_argument("--prompt-timeout", type=float, help="Maximum seconds to acknowledge and encode each style update (default: 10 real-time, 60 offline)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--offline", action="store_true", help="Wait for generated audio; does not test real-time performance")
    mode.add_argument("--coreaudio", action="store_true", help="Test real CoreAudio callbacks with speaker output silenced after recording")
    args = parser.parse_args()
    if args.duration < 3:
        parser.error("duration must be at least 3 seconds")
    if args.expected_buffer_ms is not None and not 120 <= args.expected_buffer_ms <= 5000:
        parser.error("expected-buffer-ms must be from 120 to 5000")
    if args.prompt_timeout is not None and (not math.isfinite(args.prompt_timeout) or args.prompt_timeout <= 0):
        parser.error("prompt-timeout must be finite and positive")
    root = args.directory or Path("validation") / datetime.datetime.now(datetime.timezone.utc).strftime("stream-%Y%m%dT%H%M%S-%fZ")
    return check(args.binary, args.duration, root, args.offline, args.style, args.prompt_timeout, args.coreaudio,
                 args.expected_buffer_ms)


if __name__ == "__main__":
    raise SystemExit(main())
