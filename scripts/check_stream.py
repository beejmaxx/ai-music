#!/usr/bin/env python3
"""Record an AI session, change styles, and retain audio + timing evidence."""
import argparse
import array
import datetime
import json
import math
from pathlib import Path
import re
import subprocess
import time
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build-ai/ai-music"))
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--offline", action="store_true", help="Wait for generated audio; does not test real-time performance")
    args = parser.parse_args()
    if args.duration < 3:
        parser.error("duration must be at least 3 seconds")
    root = args.directory or Path("validation") / datetime.datetime.now().strftime("stream-%Y%m%d-%H%M%S")
    root.mkdir(parents=True, exist_ok=False)
    log = root / "session.log"
    audio = root / "music.wav"
    changes = [
        (args.duration / 3, "style instrumental jazz trio, gentle piano, upright bass, brushed drums"),
        (2 * args.duration / 3, "style instrumental downtempo electronic, warm synthesizers, gentle percussion"),
    ]
    sent = []
    started = None
    wall_start = time.monotonic()
    deadline = wall_start + args.duration * (20 if args.offline else 1) + 300
    with log.open("w") as output:
        command = [
            str(args.binary.resolve()), "--source", "magenta", "--no-audio",
            "--duration", str(args.duration), "--record", str(audio),
            "--stats-every", str(1 if args.offline else min(10, args.duration / 3)),
        ]
        if args.offline:
            command.append("--offline")
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, text=True)
        try:
            while process.poll() is None:
                now = time.monotonic()
                if now > deadline:
                    raise TimeoutError(f"Stream did not finish; see {log}")
                log_text = log.read_text()
                if started is None and "Running:" in log_text:
                    started = now
                    print(f"Rendering {args.duration}s ({'offline' if args.offline else 'real time'}); recording in {root}", flush=True)
                positions = re.findall(r"\[status\] seconds=([0-9.]+)", log_text)
                position = float(positions[-1]) if positions else 0
                control_time = position if args.offline else (now - started if started is not None else 0)
                if started is not None and changes and control_time >= changes[0][0]:
                    _, command = changes.pop(0)
                    process.stdin.write(command + "\n")
                    process.stdin.flush()
                    sent.append({"elapsed_seconds": round(now - started, 2), "audio_seconds": position, "command": command})
                    print(f"Sent {command}", flush=True)
                time.sleep(.25)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdin.close()
    text = log.read_text()
    statuses = [dict(re.findall(r"([a-z_]+)=([0-9.]+)", line)) for line in text.splitlines() if line.startswith("[status]")]
    report = {"mode": "offline" if args.offline else "real_time", "wall_seconds": round(time.monotonic() - wall_start, 2),
              "returncode": process.returncode, "commands": sent, "statuses": statuses}
    if audio.exists():
        with wave.open(str(audio), "rb") as recording:
            frames = recording.getnframes()
            report["audio"] = {"frames": frames, "sample_rate": recording.getframerate(), "channels": recording.getnchannels()}
            energy, peak, count = 0, 0, 0
            while block := recording.readframes(65536):
                samples = array.array("h", block)
                count += len(samples)
                energy += sum(value * value for value in samples)
                peak = max(peak, abs(min(samples)), abs(max(samples)))
            report["audio"].update(rms=math.sqrt(energy / max(count, 1)) / 32768, peak=peak / 32768)
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Evidence: {root / 'report.json'}", flush=True)
    assert process.returncode == 0, f"Stream failed; see {log}"
    assert len(sent) == 2 and statuses, "Missing control changes or status"
    assert statuses[-1]["prompt_status"] == "2", "The final style prompt did not finish encoding"
    assert all(float(row[key]) == 0 for row in statuses for key in ("underruns", "invalid_samples", "recording_dropped")), "Audio errors"
    assert report["audio"]["frames"] == args.duration * 48000, "Incorrect recording length"
    assert report["audio"]["rms"] > 1e-5, "Recording is effectively silent"
    mode = "offline AI render" if args.offline else "real-time AI stream"
    print(f"PASS: {mode}, live style changes, complete WAV, no counted gaps.", flush=True)


if __name__ == "__main__":
    main()
