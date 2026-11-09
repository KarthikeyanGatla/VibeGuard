#!/usr/bin/env python3
"""Serve the real dashboard page with replayed engine output, no ESP32 needed.

The HTML is pulled out of firmware/VibeGuard/vg_dashboard.h and the data comes from
tests/results/timeline.jsonl (written by the host tests, so it is the firmware's own engine output).
One frame plays every 0.512 s, like the device.  Run tests/run_tests.sh first, then:

    python3 tools/dashboard_mock.py          # http://127.0.0.1:8180/
"""
import http.server
import json
import pathlib
import re
import sys
import threading
import time
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
HTML = re.search(r'R"VGHTML\((.*)\)VGHTML"', (ROOT / "firmware/VibeGuard/vg_dashboard.h").read_text(), re.S).group(1)
FRAMES = [json.loads(line) for line in (ROOT / "tests/results/timeline.jsonl").read_text().splitlines()]
FRAME_S = 0.512


class Player:
    def __init__(self):
        self.i = 0
        self.lock = threading.Lock()

    def run(self):
        while True:
            time.sleep(FRAME_S)
            with self.lock:
                self.i = (self.i + 1) % len(FRAMES)

    def jump(self, scenario):
        # Start of the first segment for that scenario after calibration (healthy -> just after calibration).
        with self.lock:
            for k, f in enumerate(FRAMES):
                if f["scenario"] == scenario and f["status"]["state"] != "CALIBRATING":
                    self.i = k
                    return

    def frame(self):
        with self.lock:
            return FRAMES[self.i]


player = Player()


class Handler(http.server.BaseHTTPRequestHandler):
    def send(self, code, body, ctype):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/":
            self.send(200, HTML, "text/html; charset=utf-8")
        elif path == "/api/status":
            self.send(200, json.dumps(player.frame()["status"]), "application/json")
        elif path == "/api/spectrum":
            self.send(200, json.dumps(player.frame()["spectrum"]), "application/json")
        else:
            self.send(404, "not found", "text/plain")

    def do_POST(self):
        url = urllib.parse.urlparse(self.path)
        if url.path == "/api/recalibrate":
            with player.lock:
                player.i = 0
        elif url.path == "/api/sim":
            player.jump(urllib.parse.parse_qs(url.query).get("scenario", ["healthy"])[0])
        self.send(200, '{"ok":true}', "application/json")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8180
    threading.Thread(target=player.run, daemon=True).start()
    print(f"VibeGuard mock dashboard on http://127.0.0.1:{port}/ ({len(FRAMES)} frames)")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
