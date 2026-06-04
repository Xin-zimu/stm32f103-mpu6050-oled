from __future__ import annotations

import argparse
import json
import mimetypes
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from attitude_state import AttitudeState
from demo_source import DemoSource
from serial_reader import start_serial_reader

APP_DIR = Path(__file__).resolve().parent
WEB_DIR = APP_DIR / "web"


def make_handler(state: AttitudeState) -> type[BaseHTTPRequestHandler]:
    class ViewerHandler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:
            return

        def do_GET(self) -> None:
            parsed = urlparse(self.path)

            if parsed.path == "/events":
                self._serve_events()
                return

            if parsed.path == "/status":
                self._send_json(state.to_dict())
                return

            path = "index.html" if parsed.path in ("", "/") else parsed.path.lstrip("/")
            file_path = (WEB_DIR / path).resolve()

            if not str(file_path).startswith(str(WEB_DIR.resolve())) or not file_path.exists():
                self.send_error(404)
                return

            content_type = mimetypes.guess_type(str(file_path))[0] or "application/octet-stream"
            body = file_path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _send_json(self, payload: dict[str, object]) -> None:
            body = json.dumps(payload).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _serve_events(self) -> None:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()

            while True:
                payload = json.dumps(state.to_dict(), separators=(",", ":"))
                try:
                    self.wfile.write(f"data: {payload}\n\n".encode("utf-8"))
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    return

                time.sleep(0.05)

    return ViewerHandler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MPU6050 3D Attitude Viewer")
    parser.add_argument("--port", help="Serial port, for example COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--demo", action="store_true", help="Use simulated ATT data")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = AttitudeState()
    stop_event = threading.Event()

    if args.demo:
        DemoSource(state, stop_event).start()
    else:
        start_serial_reader(args.port, args.baud, state, stop_event)

    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(state))
    url = f"http://{args.host}:{args.http_port}/"

    if not args.no_browser:
        webbrowser.open(url)

    print(f"MPU6050 3D Attitude Viewer: {url}")
    if args.demo:
        print("Mode: demo")
    else:
        print(f"Serial: {args.port or '(none)'} @ {args.baud}")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping MPU6050 3D Attitude Viewer.")
    finally:
        stop_event.set()
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
