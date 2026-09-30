#!/usr/bin/env python3
"""Optional model integration test: quit while a new style is being encoded."""
import argparse
import datetime
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", nargs="?", type=Path, default=Path("build-ai/ai-music"))
args = parser.parse_args()
directory = Path("validation") / datetime.datetime.now().strftime("shutdown-%Y%m%d-%H%M%S")
directory.mkdir(parents=True, exist_ok=False)
log = directory / "session.log"
with log.open("w") as output:
    process = subprocess.Popen([
        str(args.binary.resolve()), "--source", "magenta", "--no-audio",
    ], stdin=subprocess.PIPE, stdout=output, stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 180
        while "Running:" not in log.read_text():
            if process.poll() is not None:
                raise RuntimeError(f"Model startup failed; see {log}")
            if time.monotonic() > deadline:
                raise TimeoutError(f"Model startup timed out; see {log}")
            time.sleep(.01)
        process.stdin.write("style instrumental chamber strings, plucked cello, flowing arpeggios\nstatus\nquit\n")
        process.stdin.flush()
        result = process.wait(timeout=30)
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
assert re.search(r"\[status\].*prompt_status=1", text), "Did not exercise shutdown during encoding"
# Exit 2 reports real-time underruns, which are measured by check_stream.py.
# This regression test specifically checks prompt-worker lifetime and teardown.
assert result in (0, 2), f"Abnormal shutdown ({result}); see {log}"
assert text.count("[MagentaRT] Combined Prompt") >= 2 and "Error:" not in text, f"Prompt encoder did not finish before exit; see {log}"
print(f"PASS: quit during prompt encoding, normal process exit {result}. Evidence: {log}")
