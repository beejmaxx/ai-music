#!/usr/bin/env python3
"""Check Metal computation, then validate a short GPU-only AI recording."""
import argparse
import array
import datetime
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import wave

ROOT = Path(__file__).resolve().parent.parent
PROMPT = "instrumental electronic funk, steady four-on-the-floor drums, gritty synth bass, a short repeating synth riff, 112 BPM"


def run_logged(command, log, timeout, env=None):
    started = time.monotonic()
    timed_out = False
    with log.open("w") as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
        finally:
            if process.poll() is None:
                # The live validator owns a model subprocess too. Terminate
                # only this new process group so a timeout cannot orphan it.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()
    return {"returncode": process.returncode, "timed_out": timed_out,
            "wall_seconds": round(time.monotonic() - started, 3), "log": str(log)}


def audio_report(path, expected_frames):
    with wave.open(str(path), "rb") as recording:
        frames = recording.getnframes()
        if (recording.getframerate(), recording.getnchannels(), recording.getsampwidth()) != (48000, 2, 2):
            raise RuntimeError("The recording is not 48 kHz stereo PCM16")
        if frames != expected_frames:
            raise RuntimeError(f"Incomplete recording: expected {expected_frames} frames, got {frames}")
        samples = array.array("h", recording.readframes(frames))
        if sys.byteorder != "little":
            samples.byteswap()
    if len(samples) != frames * 2:
        raise RuntimeError("The WAV header describes more audio than the file contains")
    rms = math.sqrt(sum(value * value for value in samples) / max(len(samples), 1)) / 32768
    peak = max(map(abs, samples), default=0) / 32768
    if rms <= 1e-5:
        raise RuntimeError("The recording is effectively silent")
    return {"frames": frames, "sample_rate": 48000, "channels": 2, "rms": rms, "peak": peak}


def check(directory, duration, probe_only):
    directory.mkdir(parents=True, exist_ok=False)
    report = {"ok": False, "sandbox_marker": os.environ.get("CODEX_SANDBOX"),
              "requested_seconds": duration, "probe_only": probe_only}
    report_path = directory / "report.json"
    try:
        print("Checking Metal access and GPU computation...", flush=True)
        probe = directory / "metal-probe"
        report["compile"] = run_logged([
            "xcrun", "clang++", "-std=c++20", "-fobjc-arc", "-Wall", "-Wextra",
            "-framework", "Foundation", "-framework", "Metal",
            str(ROOT / "scripts/metal_probe.mm"), "-o", str(probe),
        ], directory / "compile.log", 60)
        if report["compile"]["returncode"] or report["compile"]["timed_out"]:
            raise RuntimeError("Could not build the Metal detector; see compile.log")
        report["probe"] = run_logged([str(probe)], directory / "metal.log", 60)
        try:
            report["metal"] = json.loads((directory / "metal.log").read_text())
        except (ValueError, OSError):
            raise RuntimeError("Metal did not return a diagnostic result; see metal.log")
        if not isinstance(report["metal"], dict):
            raise RuntimeError("Metal returned an unexpected diagnostic result; see metal.log")
        if (report["probe"]["returncode"] or report["probe"]["timed_out"]
                or report["metal"].get("ok") is not True):
            raise RuntimeError(report["metal"].get("error", "GPU check failed") + " No music generation attempted.")
        print(f"GPU calculation passed on {report['metal']['device']}.", flush=True)
        if not probe_only:
            binary = ROOT / "build-ai/ai-music"
            if not binary.is_file():
                raise RuntimeError("Build the Metal executable first: cmake --build build-ai --target ai-music")
            print(f"Generating {duration} seconds with Magenta on Metal; first startup may take a few minutes...", flush=True)
            audio = directory / "music.wav"
            report["generation"] = run_logged([
                str(binary), "--source", "magenta", "--offline", "--duration", str(duration),
                "--style", PROMPT, "--record", str(audio), "--no-input", "--stats-every", "5",
            ], directory / "generation.log", 300)
            if report["generation"]["returncode"] or report["generation"]["timed_out"]:
                raise RuntimeError("Metal calculation passed, but the AI render failed or timed out; see generation.log")
            log = (directory / "generation.log").read_text()
            if "Running: magenta-rt2-small (AI, Metal)" not in log:
                raise RuntimeError("The recording did not identify the expected Metal AI backend")
            statuses = [dict(re.findall(r"([a-z_]+)=([0-9.]+)", line))
                        for line in log.splitlines() if line.startswith("[status]")]
            if not statuses or any(float(row.get(key, "nan")) != 0 for row in statuses
                                   for key in ("underruns", "invalid_samples", "recording_dropped")):
                raise RuntimeError("The render reported audio gaps or invalid samples")
            if statuses[-1].get("prompt_status") != "2":
                raise RuntimeError("The style prompt did not finish encoding")
            report["audio"] = audio_report(audio, duration * 48000)
            report["generation"]["wall_seconds_per_audio_second"] = round(report["generation"]["wall_seconds"] / duration, 3)
            print(f"AI render passed: {audio}", flush=True)
            print(f"Wall time: {report['generation']['wall_seconds']:.1f}s. This is an offline test, not a live-stream test.", flush=True)
        report["ok"] = True
    except (OSError, RuntimeError, ValueError, EOFError, wave.Error) as error:
        report["error"] = str(error)
        print(str(error), file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        report["error"] = "Interrupted by user"
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Saved report: {report_path}", flush=True)
    return 0 if report["ok"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe-only", action="store_true", help="Only run the tiny Metal computation")
    parser.add_argument("--duration", type=int, default=6, help="AI audio seconds (1 to 30; default: 6)")
    args = parser.parse_args()
    if not 1 <= args.duration <= 30:
        parser.error("duration must be between 1 and 30 seconds")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ")
    return check(ROOT / "validation" / f"gpu-check-{stamp}", args.duration, args.probe_only)


if __name__ == "__main__":
    raise SystemExit(main())
