#!/usr/bin/env python3
"""Validate live PCM, local controls, score rejection, and browser isolation."""
import importlib.util
import json
from pathlib import Path
import socket
import struct
import tempfile
import threading
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("station", ROOT / "scripts" / "station.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def main():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="ai-music-web-") as directory:
        watch = Path(directory) / "current.commands"
        watch.write_text("# Start with the automatic director\n")
        station = module.Station(ROOT / "build" / "ai-music", watch)
        server = module.serve(station, port)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{port}"

        def get(path):
            with urllib.request.urlopen(base + path, timeout=3) as response:
                return response.read()

        def post(path, commands, origin=base):
            request = urllib.request.Request(base + path,
                json.dumps({"commands": commands}).encode(),
                {"Content-Type": "application/json", "Origin": origin})
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status

        def wait(predicate):
            deadline = time.monotonic() + 6
            while time.monotonic() < deadline:
                status = json.loads(get("/status"))
                if predicate(status):
                    return status
                time.sleep(.1)
            raise AssertionError(station.snapshot())

        try:
            assert b"afterhours" in get("/")
            initial = wait(lambda state: state.get("frames", 0) > 0)
            # This is newly generated PCM from the live engine, not a file endpoint.
            with urllib.request.urlopen(base + "/audio", timeout=4) as stream:
                raw = stream.read(48000)
            pcm = struct.unpack("<" + "h" * (len(raw) // 2), raw)
            assert sum(value * value for value in pcm) / len(pcm) > 100
            assert post("/control", "volume .2") == 202
            current = wait(lambda state: state.get("volume") == .2)
            revision = current["score"]
            assert post("/score", "at 0 melody 0 - 2 - 3 - 2 - 4 - 3 - 2 - 0 -\nramp 0 1 mix lead .4") == 202
            accepted = wait(lambda state: state.get("score", 0) > revision)
            assert accepted["frames"] > initial["frames"]
            assert post("/score", "volume 0\ntempo nan") == 202
            rejected = wait(lambda state: "retained" in state.get("error", ""))
            assert rejected["volume"] == .2 and rejected["score"] == accepted["score"]
            try:
                post("/control", "volume 0", origin="https://unrelated.example")
                raise AssertionError("Cross-origin command accepted")
            except urllib.error.HTTPError as error:
                assert error.code == 403
            assert post("/control", "cancel") == 202
            wait(lambda state: state.get("radio") == 0)
            assert post("/control", "next") == 202
            final = wait(lambda state: state.get("radio") == 1)
            assert final["underruns"] == final["invalid_samples"] == final["stream_dropped"] == 0
        finally:
            server.shutdown()
            server.server_close()
            station.close()
            thread.join(timeout=2)
        assert station.process.returncode == 0
    print("Passed: continuous live PCM, web controls, atomic score rejection, local-origin protection, clean shutdown.")


if __name__ == "__main__":
    main()
