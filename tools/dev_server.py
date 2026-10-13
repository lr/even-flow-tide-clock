#!/usr/bin/env python3
"""
Serve the settings page on the Mac, standing in for the board.

    python3 tools/dev_server.py            # http://localhost:8080
    python3 tools/dev_server.py --lan      # also reachable from your phone on the same WiFi

Same two requests the board will answer:

    GET  /api/config   the settings as key=value text
    POST /api/config   new settings as key=value text; checked by the board's own
                       parser (build/even_flow_config_check), then saved

Saved settings go to even-flow.conf in the project root (or --config FILE).
A running simulator using that file picks the change up within a second.

Standard library only. Build first: cmake --build build
"""
import argparse
import http.server
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
CHECKER = os.path.join(ROOT, "build", "even_flow_config_check")
MAX_BODY = 8192

TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript", ".css": "text/css",
         ".json": "application/json", ".svg": "image/svg+xml"}


def check(text):
    """Run the board's parser. Returns (ok, canonical text or problem)."""
    r = subprocess.run([CHECKER], input=text.encode(), capture_output=True, timeout=5)
    return r.returncode == 0, r.stdout.decode().strip() + ("\n" if r.returncode == 0 else "")


class Handler(http.server.BaseHTTPRequestHandler):
    config_path = None

    def send(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/api/config":
            if os.path.exists(self.config_path):
                with open(self.config_path) as f:
                    self.send(200, f.read())
            else:
                ok, text = check("")  # defaults
                self.send(200, text)
            return

        # Static files, only from web/
        rel = "index.html" if path == "/" else path.lstrip("/")
        full = os.path.realpath(os.path.join(WEB, rel))
        if not full.startswith(os.path.realpath(WEB) + os.sep) or not os.path.isfile(full):
            self.send(404, "not found")
            return
        with open(full, "rb") as f:
            self.send(200, f.read(), TYPES.get(os.path.splitext(full)[1], "application/octet-stream"))

    def do_POST(self):
        if self.path != "/api/config":
            self.send(404, "not found")
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            self.send(413 if length > MAX_BODY else 400, "settings missing or too long")
            return
        text = self.rfile.read(length).decode("utf-8", errors="replace")

        ok, result = check(text)
        if not ok:
            self.send(400, result)
            return

        # Write atomically so the simulator never reads half a file
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(self.config_path), prefix=".even-flow-")
        with os.fdopen(fd, "w") as f:
            f.write(result)
        os.replace(tmp, self.config_path)
        print(f"saved {self.config_path}")
        self.send(200, result)

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.command, self.path))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--config", default=os.path.join(ROOT, "even-flow.conf"))
    ap.add_argument("--lan", action="store_true", help="listen on all interfaces, not just this Mac")
    args = ap.parse_args()

    if not os.access(CHECKER, os.X_OK):
        sys.exit(f"missing {CHECKER}; build first: cmake --build build")

    Handler.config_path = os.path.abspath(args.config)
    host = "0.0.0.0" if args.lan else "127.0.0.1"
    server = http.server.ThreadingHTTPServer((host, args.port), Handler)
    print(f"settings page on http://{'<this Mac>' if args.lan else 'localhost'}:{args.port}/ -> {Handler.config_path}")
    server.serve_forever()


if __name__ == "__main__":
    main()
