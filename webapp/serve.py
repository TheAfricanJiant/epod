#!/usr/bin/env python3
"""
ePod signage - local server.

Serves this folder on http://localhost:8000 and forwards every /api/* request
to the ePod at 192.168.4.1.

The proxy is the point. The page is served from localhost but the device lives
on another origin, so a direct fetch() would be a cross-origin request and the
browser would block it. Going through here makes both look like one origin, and
it means nothing has to be configured in the browser on the day.

Standard library only - no pip install, any machine with Python 3.

    python serve.py                 # localhost:8000, device at 192.168.4.1
    python serve.py --port 9000
    python serve.py --device 192.168.4.1
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

DEVICE = "192.168.4.1"
TIMEOUT = 1.5          # the ePod is on the far side of a Soft-AP; keep it short
                       # so a missing device degrades to demo mode immediately
                       # instead of hanging the page.


class Handler(SimpleHTTPRequestHandler):
    # Serve out of the script's own folder no matter where it was launched from.
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=os.path.dirname(os.path.abspath(__file__)), **kw)

    def log_message(self, fmt, *args):
        # One line per poll would be four lines a second. Only report problems.
        pass

    # ---- proxy ------------------------------------------------------------
    def _proxy(self, body=None):
        url = "http://%s%s" % (DEVICE, self.path)
        req = urllib.request.Request(url, data=body,
                                     method="POST" if body is not None else "GET")
        if body is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                payload = r.read()
                code = r.status
        except (urllib.error.URLError, OSError, TimeoutError) as e:
            # Not an error worth shouting about: it just means the ePod is not
            # on the network yet. The page falls back to demo mode on its own.
            payload = json.dumps({"error": str(e)}).encode()
            code = 503

        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path.startswith("/api/"):
            return self._proxy()
        return super().do_GET()

    def do_POST(self):
        if self.path.startswith("/api/"):
            n = int(self.headers.get("Content-Length") or 0)
            return self._proxy(self.rfile.read(n) if n else b"")
        self.send_error(404)


def main():
    global DEVICE
    p = argparse.ArgumentParser(description="ePod signage local server")
    p.add_argument("--port", type=int, default=8000)
    p.add_argument("--device", default=DEVICE, help="ePod IP (default %s)" % DEVICE)
    a = p.parse_args()
    DEVICE = a.device

    try:
        srv = ThreadingHTTPServer(("0.0.0.0", a.port), Handler)
    except OSError as e:
        sys.exit("Could not bind port %d: %s\nTry: python serve.py --port 8080" % (a.port, e))

    print("  ePod signage")
    print("  open      http://localhost:%d" % a.port)
    print("  device    %s  (proxied at /api/*)" % DEVICE)
    print("  Ctrl+C to stop")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n  stopped")


if __name__ == "__main__":
    main()
