#!/usr/bin/env python3
"""Local browser player for the live C++ station. Standard library only."""
import argparse
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


class Station:
    def __init__(self, binary, watch, volume=.4):
        self.watch = watch
        self.lock = threading.Lock()
        self.commands_lock = threading.Lock()
        self.listeners = set()
        self.status = {"chapter": "Starting the station", "error": ""}
        self.logs = deque(maxlen=40)
        read_fd, write_fd = os.pipe()
        try:
            self.process = subprocess.Popen([
                str(binary), "--source", "synth", "--radio", "--volume", str(volume),
                "--watch", str(watch), "--no-audio", "--stats-every", ".5",
                "--stream-fd", str(write_fd),
            ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1, pass_fds=(write_fd,), cwd=ROOT)
        except BaseException:
            os.close(read_fd)
            raise
        finally:
            os.close(write_fd)
        self.reader = threading.Thread(target=self.read_audio, args=(read_fd,), daemon=True)
        self.logger = threading.Thread(target=self.read_log, daemon=True)
        self.reader.start()
        self.logger.start()

    def read_audio(self, fd):
        try:
            with os.fdopen(fd, "rb", buffering=0) as source:
                while block := source.read(4096):
                    with self.lock:
                        for listener in tuple(self.listeners):
                            try:
                                listener.put_nowait(block)
                            except queue.Full:
                                # A stalled browser must never stall the audio engine.
                                self.listeners.discard(listener)
                                while not listener.empty():
                                    try:
                                        listener.get_nowait()
                                    except queue.Empty:
                                        break
                                listener.put_nowait(None)
        finally:
            with self.lock:
                for listener in self.listeners:
                    try:
                        listener.put_nowait(None)
                    except queue.Full:
                        pass

    def read_log(self):
        for raw in self.process.stdout:
            line = raw.strip()
            # Keep pane 2 readable; browser receives half-second telemetry.
            if not line.startswith("[status]"):
                print(line, flush=True)
            with self.lock:
                self.logs.append(line)
                if line.startswith("[status]"):
                    for field in line.split()[1:]:
                        key, _, value = field.partition("=")
                        try:
                            self.status[key] = float(value)
                        except ValueError:
                            pass
                elif line.startswith("[radio]") and " — " in line:
                    self.status["chapter"] = line[8:].split(" — ")[0]
                elif line.startswith("[command]") or (line.startswith("[watch]") and "retained" in line):
                    self.status["error"] = line
                elif line.startswith("[watch] Applied"):
                    self.status["error"] = ""

    def snapshot(self):
        with self.lock:
            return {**self.status, "running": self.process.poll() is None,
                    "listeners": len(self.listeners), "logs": list(self.logs)[-6:]}

    def command(self, text, score=False):
        if not isinstance(text, str) or not text.strip() or len(text.encode()) > 16000:
            raise ValueError("Commands must contain 1–16000 bytes")
        with self.commands_lock:
            if self.process.poll() is not None:
                raise ValueError("The audio engine has stopped")
            if score:
                with tempfile.NamedTemporaryFile(mode="w", dir=self.watch.parent, delete=False) as temp:
                    temp.write(text + "\n")
                    name = temp.name
                try:
                    os.replace(name, self.watch)
                finally:
                    if os.path.exists(name):
                        os.unlink(name)
            else:
                if "\n" in text or "\r" in text or text.split()[0] not in {
                    "mix", "volume", "tempo", "filter", "delay", "mute", "unmute", "next", "radio", "cancel",
                }:
                    raise ValueError("Use one live control, or submit a score")
                self.process.stdin.write(text + "\n")
                self.process.stdin.flush()
            with self.lock:
                self.status["error"] = ""

    def close(self):
        if self.process.poll() is None:
            try:
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.terminate()
                self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        self.logger.join(timeout=2)


def serve(station, port):
    origins = {f"http://127.0.0.1:{port}", f"http://localhost:{port}"}
    hosts = {f"127.0.0.1:{port}", f"localhost:{port}"}

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def respond(self, code, data, kind="application/json"):
            if not isinstance(data, bytes):
                data = json.dumps(data).encode()
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(data)

        def allowed(self):
            if self.headers.get("Host") not in hosts or self.headers.get("Origin", "") not in origins | {""}:
                self.respond(403, {"error": "This player accepts local, same-origin requests only"})
                return False
            return True

        def do_GET(self):
            if not self.allowed():
                return
            files = {"/": ("index.html", "text/html; charset=utf-8"),
                     "/app.js": ("app.js", "text/javascript"),
                     "/worklet.js": ("worklet.js", "text/javascript")}
            if self.path in files:
                name, kind = files[self.path]
                self.respond(200, (ROOT / "web" / name).read_bytes(), kind)
            elif self.path == "/status":
                self.respond(200, station.snapshot())
            elif self.path == "/audio":
                listener = queue.Queue(maxsize=32)
                with station.lock:
                    full = len(station.listeners) >= 4
                    if not full:
                        station.listeners.add(listener)
                if full:
                    self.respond(503, {"error": "This personal player supports four local connections"})
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                self.connection.settimeout(3)
                try:
                    while station.process.poll() is None:
                        block = listener.get(timeout=3)
                        if block is None:
                            break
                        self.wfile.write(block)
                        self.wfile.flush()
                except (OSError, queue.Empty):
                    pass
                finally:
                    with station.lock:
                        station.listeners.discard(listener)
            else:
                self.respond(404, {"error": "Not found"})

        def do_POST(self):
            if not self.allowed():
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if self.path not in {"/control", "/score"} or not 0 < length <= 20000:
                    raise ValueError("Invalid control request")
                if self.headers.get("Content-Type") != "application/json":
                    raise ValueError("Use application/json")
                self.connection.settimeout(3)
                body = json.loads(self.rfile.read(length))
                station.command(body["commands"], score=self.path == "/score")
                self.respond(202, {"message": "Queued for the live engine"})
            except (ValueError, KeyError, TypeError, OSError) as error:
                self.close_connection = True
                self.respond(400, {"error": str(error)})

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8799)
    parser.add_argument("--volume", type=float, default=.4)
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "ai-music")
    parser.add_argument("--watch", type=Path, default=ROOT / "live" / "current.commands")
    args = parser.parse_args()
    if not 0 <= args.volume <= 1:
        parser.error("Volume must be between 0 and 1")
    station = Station(args.binary.resolve(), args.watch.resolve(), args.volume)
    try:
        server = serve(station, args.port)
        print(f"Listen and mix: http://127.0.0.1:{args.port} — click Listen. Ctrl-C stops the station.", flush=True)
        try:
            server.serve_forever(poll_interval=.25)
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    finally:
        station.close()


if __name__ == "__main__":
    main()
