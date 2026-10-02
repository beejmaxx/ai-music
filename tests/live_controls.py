#!/usr/bin/env python3
"""Exercise saved-file controls and malformed edits while real-time audio continues."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import time
import wave
from functional_host import require_functional_exit

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--allow-scheduler-delays", action="store_true",
                    help="Check functionality on shared runners while reporting software timer misses")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="ai-music-controls-") as directory:
    root = Path(directory)
    control = root / "live.commands"
    recording = root / "take.wav"
    log = root / "session.log"
    control.write_text("style ambient\nvolume .25\n")
    with log.open("w") as output:
        process = subprocess.Popen([
            str(args.binary.resolve()), "--no-audio", "--no-input", "--duration", "5",
            "--watch", str(control), "--record", str(recording), "--stats-every", ".25",
        ], stdout=output, stderr=subprocess.STDOUT)
        try:
            # Start timing edits only after the player is running.
            deadline = time.monotonic() + 15
            while "Running:" not in log.read_text():
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(log.read_text())
                time.sleep(.05)
            time.sleep(.6)
            control.write_text("style pulse\nvolume .35\ntempo 118\ndrums on\n")
            time.sleep(.9)
            # Validation must reject the whole file, including volume 0.
            control.write_text("volume 0\ntempo nan\n")
            time.sleep(.9)
            control.write_text("style ambient\nvolume .20\ndrums off\n")
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    text = log.read_text()
    statuses = [dict(re.findall(r"([a-z_]+)=(\S+)", line))
                for line in text.splitlines() if line.startswith("[status]")]
    if not statuses:
        raise RuntimeError(text)
    misses = require_functional_exit(process.returncode, statuses[-1], args.allow_scheduler_delays)
    assert text.count("[watch] Applied") == 3, text
    assert "current settings retained" in text, text
    assert "volume=0.00" not in text, text
    assert "volume=0.35" in text and "volume=0.20" in text, text
    assert "underruns=0" in text and "recording_dropped=0" in text, text
    with wave.open(str(recording), "rb") as audio:
        assert audio.getnframes() == 5 * 48000
        assert audio.getnchannels() == 2 and audio.getsampwidth() == 2
    print("Passed: live edits, atomic rejection of malformed controls, complete 5-second WAV.")
    if misses:
        print(f"Functional check only: {misses} software timer misses retained; playback timing is not certified.")
