#!/usr/bin/env python3
"""Serve web/index.html with a fake OmniPad API, to work on the dashboard without a PS5.

Slot 1 is a DualShock 4 on a cable that never stops reporting, cycling through every button.
Slot 2 is an 8BitDo dongle that only reports on change: it moves for a few seconds, then goes
quiet long enough to show the 60s idle countdown. Slot 3 is free.

With --proxy, /api/* goes to a real OmniPad instead (the page is still served from disk), so the
dashboard can be worked on against live controllers without rebuilding the payload.

Usage: python tools/mock_dashboard.py [port] [--proxy http://<PS5_IP>:8095]
       (default port 8095, then open http://localhost:<port>/)
"""
import json
import math
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

PROXY = None

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "web" / "index.html"
START = time.time()
BITS = [0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 20]

log_lock = threading.Lock()
log_text = ""


def log(msg):
    global log_text
    t = time.localtime()
    ms = int((time.time() % 1) * 1000)
    with log_lock:
        log_text += f"[{t.tm_hour:02d}:{t.tm_min:02d}:{t.tm_sec:02d}.{ms:03d}] {msg}\n"


def now_ms():
    return int((time.time() - START) * 1000)


def ds4_slot(t):
    phase = int(t / 0.6) % len(BITS)
    a = t * 1.3
    return {
        "status": 3, "name": "Sony DualShock 4 (USB)", "conn": 3, "vid": 0x054C, "pid": 0x05C4,
        "user_id": 0x10000001, "battery": 100, "charging": 1,
        "injected": int(t * 250), "inputs": int(t * 250), "input_age_ms": 4,
        "connected_ms": int(t * 1000), "buttons": 1 << BITS[phase],
        "lx": int(128 + 120 * math.cos(a)), "ly": int(128 + 120 * math.sin(a)),
        "rx": int(128 + 90 * math.sin(a * 0.7)), "ry": 128,
        "l2": int(127 + 127 * math.sin(t * 2)), "r2": int(127 + 127 * math.cos(t * 2)),
    }


class Dongle:
    """Event-driven pad: active 6s out of every 80s, silent otherwise."""

    def __init__(self):
        self.inputs = 0
        self.last_input = 0.0

    def slot(self, t):
        cycle = t % 80
        active = cycle < 6
        if active:
            self.inputs += 3
            self.last_input = t
        age = int((t - self.last_input) * 1000)
        return {
            "status": 3, "name": "XInput Compatible Controller", "conn": 4, "vid": 0x2DC8, "pid": 0x3106,
            "user_id": 0x10000002, "battery": 100, "charging": 0,
            "injected": int(t * 250), "inputs": self.inputs, "input_age_ms": age,
            "connected_ms": int(t * 1000),
            "buttons": (1 << 14) if active and int(t * 2) % 2 else 0,
            "lx": 128, "ly": 128 if not active else int(128 + 100 * math.sin(t * 3)),
            "rx": 128, "ry": 128, "l2": 0, "r2": 255 if active and cycle > 4 else 0,
        }


dongle = Dongle()
FREE = {"status": 0, "name": "", "conn": 0, "vid": 0, "pid": 0, "user_id": 0, "battery": 0, "charging": 0,
        "injected": 0, "inputs": 0, "input_age_ms": -1, "connected_ms": -1,
        "buttons": 0, "lx": 128, "ly": 128, "rx": 128, "ry": 128, "l2": 0, "r2": 0}


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype, extra=None):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def _proxy(self, method):
        req = urllib.request.Request(PROXY + self.path, method=method, data=b"" if method == "POST" else None)
        try:
            with urllib.request.urlopen(req, timeout=3) as res:
                extra = {k: v for k, v in res.headers.items() if k in ("X-Log-Offset",)}
                self._send(res.status, res.read(), res.headers.get("Content-Type", "text/plain"), extra)
        except (urllib.error.URLError, TimeoutError) as e:
            self._send(502, f"proxy error: {e}", "text/plain")

    def do_GET(self):
        url = urlparse(self.path)
        t = time.time() - START
        if PROXY and url.path.startswith("/api/"):
            self._proxy("GET")
        elif url.path == "/api/state":
            state = {"version": "1.0.4-mock", "build": 0, "uptime_ms": now_ms(), "now_ms": now_ms(),
                     "slots": [ds4_slot(t), dongle.slot(t), FREE]}
            self._send(200, json.dumps(state), "application/json")
        elif url.path == "/api/status":
            self._send(200, json.dumps({"version": "1.0.4-mock", "build": 0, "slots": []}), "application/json")
        elif url.path == "/api/log":
            with log_lock:
                data = log_text.encode()
            since = parse_qs(url.query).get("since")
            start = int(since[0]) if since and 0 <= int(since[0]) <= len(data) else max(0, len(data) - 32768)
            chunk = data[start:start + 32768]
            self._send(200, chunk, "text/plain; charset=utf-8", {"X-Log-Offset": str(start + len(chunk))})
        else:
            self._send(200, PAGE.read_bytes(), "text/html; charset=UTF-8")

    def do_POST(self):
        if PROXY:
            self._proxy("POST")
            return
        log(f"web: mock received POST {self.path}")
        self._send(200, '{"status":"ok"}', "application/json")

    def log_message(self, *args):
        pass


def background_log():
    log("OmniPad PS5 v1.0.4-mock (Build 0)")
    log("usb_hotplug: Device discovered on /dev/ugen1.2 (VID 0x054c, PID 0x05c4, type 2: Sony DualShock 4 (USB))")
    log("vpad: slot 0 ready (handle 1, alt -1, dev 0x1, user 0x10000001)")
    log("usb_hotplug: Device discovered on /dev/ugen1.3 (VID 0x2dc8, PID 0x3106, type 4: XInput Compatible Controller)")
    log("usb_hotplug: XInput player LED sent on OUT for slot 1 -> 3")
    log("vpad: slot 1 AddDevice -> 0x803b0006")
    log("vpad: slot 1 ready (handle 2, alt -1, dev 0x2, user 0x10000002)")
    n = 0
    while True:
        time.sleep(3)
        n += 1
        log(f"usb_hotplug: slot 1 idle/standby (n=0, use_fs=1)" if n % 2 else f"usb_hotplug: slot 0 button press 0x{1 << BITS[n % len(BITS)]:08x} (LX=128 LY=128)")


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--proxy" in args:
        i = args.index("--proxy")
        PROXY = args[i + 1].rstrip("/")
        del args[i:i + 2]
    port = int(args[0]) if args else 8095
    if not PROXY:
        threading.Thread(target=background_log, daemon=True).start()
    print(f"dashboard on http://localhost:{port}/" + (f" (API proxied to {PROXY})" if PROXY else " (simulated API)"))
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
