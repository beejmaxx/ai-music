#!/usr/bin/env python3
"""Compare inference settings, test a sustained live stream, optionally play it."""
import argparse
import datetime
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys

from check_gpu import ROOT, run_logged
from check_stream import INFERENCE_ENV_KEYS

PRESETS = (
    ("baseline", {"AI_MUSIC_CACHE_CONSTANTS": "0"}),
    ("cached", {"AI_MUSIC_CACHE_CONSTANTS": "1"}),
    ("cached-batched", {"AI_MUSIC_CACHE_CONSTANTS": "1",
                        "MLX_MAX_OPS_PER_BUFFER": "128", "MLX_MAX_MB_PER_BUFFER": "128"}),
)
PROMPT = "instrumental electronic funk and techno, driving four-on-the-floor drums, gritty synth bass, evolving synth riff, 124 BPM"


def environment(settings):
    result = os.environ.copy()
    for key in INFERENCE_ENV_KEYS:
        result.pop(key, None)
    result.update(settings)
    return result


def benchmark_report(path, steps):
    data = json.loads(path.read_text())
    if not isinstance(data, dict) or data.get("ok") is not True or data.get("steps") != steps:
        raise RuntimeError("Benchmark did not complete the requested frames")
    for key in ("mean_ms", "p95_ms", "measured_seconds", "realtime_factor", "rms"):
        value = data.get(key)
        if not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise RuntimeError(f"Invalid benchmark metric: {key}")
    if not math.isclose(data["realtime_factor"], data["measured_seconds"] / (steps * .04), rel_tol=1e-6):
        raise RuntimeError("Benchmark timing does not match generated audio length")
    return data


def best_candidate(profiles):
    # Mean throughput alone cannot certify live playback. This only chooses
    # which settings get the subsequent real-time test, including style changes.
    candidates = [row for row in profiles if row.get("metrics", {}).get("realtime_factor", math.inf) < 1]
    return min(candidates, key=lambda row: row["metrics"]["realtime_factor"], default=None)


def live_report(path, duration, style):
    data = json.loads(path.read_text())
    if (not isinstance(data, dict) or data.get("ok") is not True
            or data.get("mode") != "coreaudio" or data.get("returncode") != 0
            or data.get("requested_seconds") != duration or data.get("initial_style") != style
            or not isinstance(data.get("buffer"), dict)
            or data.get("buffer", {}).get("actual_frames") != 240000
            or not isinstance(data.get("coreaudio_phases"), dict)
            or set(data.get("coreaudio_phases", {})) != {"warmup", "active"}):
        raise RuntimeError("Test did not establish CoreAudio AI playback with the requested style")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--play", action="store_true", help="Start continuous speaker playback only after the live test passes")
    parser.add_argument("--steps", type=int, default=250, help="Measured 40 ms frames per configuration (default 250)")
    parser.add_argument("--duration", type=int, default=120, help="Sustained real-time test seconds (default 120)")
    args = parser.parse_args()
    if not 100 <= args.steps <= 15000 or not 60 <= args.duration <= 3600:
        parser.error("Use 100..15000 steps and 60..3600 test seconds")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ")
    directory = ROOT / "validation" / f"live-tuning-{stamp}"
    directory.mkdir(parents=True)
    report = {"ok": False, "profiles": [], "test_seconds": args.duration,
              "sandbox_marker": os.environ.get("CODEX_SANDBOX")}
    report_path = directory / "report.json"
    candidate = None
    try:
        for binary in ("music-benchmark", "ai-music"):
            if not (ROOT / "build-ai" / binary).is_file():
                raise RuntimeError("Build first: cmake --build build-ai --target ai-music music-benchmark --parallel 2")
        for name, command in (("power", ["pmset", "-g", "custom"]),
                              ("power_source", ["pmset", "-g", "batt"])):
            report[name] = run_logged(command, directory / f"{name}.log", 10)
        power = (directory / "power.log").read_text()
        if any(line.strip().startswith("lowpowermode") and line.strip().endswith("1") for line in power.splitlines()):
            print("Low Power Mode is enabled. Results will reflect that power limit.", flush=True)
        model = ROOT / "models/models/mrt2_small/mrt2_small.mlxfn"
        print(f"Evidence: {directory}\nComparing three inference configurations; speaker output stays silent during testing.", flush=True)
        for name, settings in PRESETS:
            print(f"Profiling {name}...", flush=True)
            csv = directory / f"{name}.csv"
            execution = run_logged([
                str(ROOT / "build-ai/music-benchmark"), str(model), str(args.steps), "25", str(csv),
            ], directory / f"{name}.log", max(180, args.steps * .5), env=environment(settings))
            row = {"name": name, "settings": settings, "execution": execution}
            report["profiles"].append(row)
            if execution["returncode"] or execution["timed_out"]:
                # No device or failed inference is not a slow-but-valid result.
                raise RuntimeError(f"{name} failed; see {name}.log")
            row["metrics"] = benchmark_report(csv.with_suffix(".json"), args.steps)
            metrics = row["metrics"]
            print(f"  Mean {metrics['mean_ms']:.1f} ms, p95 {metrics['p95_ms']:.1f} ms; "
                  f"throughput {1 / metrics['realtime_factor']:.2f}x playback speed.", flush=True)
            report_path.write_text(json.dumps(report, indent=2) + "\n")
        candidate = best_candidate(report["profiles"])
        if candidate is None:
            raise RuntimeError("All configurations remain slower than playback. Profiles saved; live generation is not fixed yet.")
        report["selected"] = candidate["name"]
        report["settings"] = candidate["settings"]
        print(f"Testing {candidate['name']} through CoreAudio for {args.duration} seconds with silent speaker output and two prompt changes...", flush=True)
        live_dir = directory / "live"
        report["live_test"] = run_logged([
            sys.executable, str(ROOT / "scripts/check_stream.py"),
            "--binary", str(ROOT / "build-ai/ai-music"), "--duration", str(args.duration),
            "--directory", str(live_dir), "--style", PROMPT, "--coreaudio", "--expected-buffer-ms", "5000",
        ], directory / "live-test.log", args.duration + 360, env=environment(candidate["settings"]))
        if report["live_test"]["returncode"] or report["live_test"]["timed_out"]:
            raise RuntimeError("Sustained live test failed. See live/report.json and live/session.log; playback not started.")
        live = live_report(live_dir / "report.json", args.duration, PROMPT)
        report["prompt_latencies_seconds"] = [row["latency_seconds"] for row in live["commands"]]
        report["ok"] = True
        report["coreaudio_phases"] = live["coreaudio_phases"]
        print(f"PASS: {args.duration}s of AI generation through CoreAudio, two encoded prompt updates, "
              "no counted playback gaps, active callback errors, active device overloads, or invalid samples. "
              "Silent preparation counters are retained in coreaudio_phases.warmup.", flush=True)
    except (OSError, ValueError, RuntimeError) as error:
        report["error"] = str(error)
        print(str(error), file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        report["error"] = "Interrupted by user"
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(f"Saved report: {report_path}", flush=True)
    if not report["ok"]:
        return 1
    if args.play:
        print("Starting continuous AI generation through the speakers. Type a style prompt as 'style ...'; Ctrl-C stops.", flush=True)
        command = [str(ROOT / "build-ai/ai-music"), "--source", "magenta", "--style", PROMPT,
                   "--record", str(directory / "listening.wav"), "--stats-every", "10"]
        process = subprocess.Popen(command, cwd=ROOT, env=environment(candidate["settings"]))
        try:
            return process.wait()
        except KeyboardInterrupt:
            # The foreground child also receives Ctrl-C. Give the C++ host time
            # to stop inference and finalize its WAV instead of killing it.
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
