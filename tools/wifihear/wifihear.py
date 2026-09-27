#!/usr/bin/env python3
"""WifiHear helper — HTML dashboard for a 2.4 GHz *scan* on this PC's CDC.

Authorized survey remainder of AirMaraud. This process does not transmit
deauth or any other 802.11 frame; it only reads DIAG/WH lines from the
OG main CPU and serves them on localhost.

    python tools/wifihear/wifihear.py

Never open the port at 1200 baud. That is BOOTSEL, not a console.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_HERE = pathlib.Path(__file__).resolve().parent
_TOOLS = _HERE.parent
for _p in (_TOOLS, _HERE):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

import fw  # noqa: E402

CDC_BAUD = 115200
BOOTSEL_BAUD = 1200
WIFIHEAR_TOKENS = ("wifihear",)

# BN WIFI AP ssid=Cafe bssid=aabbccddeeff ch=6 rssi=-42 auth=wpa2
# WH AP ssid=Cafe bssid=aabbccddeeff ch=6 rssi=-42 auth=wpa2
_KV = re.compile(r"(?P<k>[A-Za-z_]+)=(?P<v>\S+)")
_AP = re.compile(
    r"^(?:\[wifihear\]\s+)?(?:BN WIFI |WH )AP\b(?P<rest>.*)$", re.I
)
_STA = re.compile(
    r"^(?:\[wifihear\]\s+)?(?:BN WIFI |WH )STA\b(?P<rest>.*)$", re.I
)
_SCAN = re.compile(
    r"^(?:\[wifihear\]\s+)?(?:BN WIFI |WH )(?P<st>on|off)\b", re.I
)


class WifiHearError(RuntimeError):
    """Port identification or baud refusal."""


def _int_or_none(s):
    if s is None:
        return None
    t = str(s).strip()
    if not t or t[0] not in "-0123456789":
        return None
    if t[0] == "-" and not t[1:].isdigit():
        return None
    if t[0] != "-" and not t.isdigit():
        return None
    return int(t)


def _kv(rest):
    out = {}
    for m in _KV.finditer(rest or ""):
        out[m.group("k").lower()] = m.group("v")
    return out


def parse_wifihear_line(line):
    """Parse one CDC line into a dict, or None if it is not survey data."""
    text = (line or "").strip().lstrip("\x00").lstrip("\r")
    m = _SCAN.match(text)
    if m:
        return {"kind": "SCAN", "on": m.group("st").lower() == "on"}
    m = _AP.match(text)
    if m:
        kv = _kv(m.group("rest"))
        if not kv.get("bssid"):
            return None
        return {
            "kind": "AP",
            "ssid": kv.get("ssid", "-"),
            "bssid": kv["bssid"].lower(),
            "ch": _int_or_none(kv.get("ch")),
            "rssi": _int_or_none(kv.get("rssi")),
            "auth": kv.get("auth", "-"),
        }
    m = _STA.match(text)
    if m:
        kv = _kv(m.group("rest"))
        if not kv.get("mac"):
            return None
        return {
            "kind": "STA",
            "mac": kv["mac"].lower(),
            "bssid": (kv.get("bssid") or "-").lower(),
            "rssi": _int_or_none(kv.get("rssi")),
        }
    return None


def is_wifihear_product(product):
    p = (product or "").lower()
    return any(t in p for t in WIFIHEAR_TOKENS)


def _fmt_port(p):
    pid = f"{p.pid:04X}" if p.pid is not None else "?"
    vid = f"{p.vid:04X}" if p.vid is not None else "?"
    return f"{p.device} ({vid}:{pid} {p.product or '?'})"


def pick_wifihear_main(ports):
    """The WifiHear *main* CDC. Display is LCD-only; C6 UART is on main."""
    device = fw._pick_cpu_port(ports, "main")
    port = next(p for p in ports if p.device == device)
    if port.pid == fw.CPU_PID["display"]:
        raise WifiHearError(
            f"{_fmt_port(port)} has the DISPLAY PID 2055. WifiHear survey "
            "lines live on the main CPU (PID 2054). Refusing."
        )
    if port.product and not is_wifihear_product(port.product):
        # Firmware is not in tree yet; still allow a named main app so the
        # dashboard can be exercised against a hand-typed CDC, but warn.
        pass
    return port


def resolve_wifihear_port(ports, port_arg=None):
    if port_arg:
        matches = [p for p in ports
                   if (p.device or "").upper() == port_arg.upper()]
        if not matches:
            raise WifiHearError(
                f"port {port_arg} is not in the serial listing"
            )
        p = matches[0]
        if p.vid == fw.ICS_USB_VID and p.pid == fw.CPU_PID["display"]:
            raise WifiHearError(
                f"{_fmt_port(p)} is the DISPLAY CPU (PID 2055). Survey "
                "lines are on main (PID 2054). Refusing."
            )
        return p
    return pick_wifihear_main(ports)


def open_cdc(device, baud=CDC_BAUD):
    if baud == BOOTSEL_BAUD:
        raise WifiHearError(
            "refusing to open at 1200 baud: that reboots the RP2040 into "
            "BOOTSEL, which is recovery, not the WifiHear console"
        )
    try:
        import serial
    except ImportError as e:
        raise WifiHearError(
            "pyserial is required:  python -m pip install pyserial"
        ) from e
    ser = serial.Serial()
    ser.port = device
    ser.baudrate = baud
    ser.timeout = 0.2
    ser.dsrdtr = False
    ser.open()
    ser.dtr = True
    time.sleep(0.2)
    ser.reset_input_buffer()
    return ser


INDEX_HTML = """<!doctype html>
<html lang="en">
<meta charset="utf-8">
<title>WifiHear — authorized 2.4 GHz survey</title>
<style>
  body { font-family: sans-serif; max-width: 52em; margin: 1.5em auto;
         background: #12151c; color: #e6e8ee; }
  h1 { font-size: 1.3rem; color: #50c878; }
  .note { color: #9aa3b2; }
  table { border-collapse: collapse; width: 100%; margin: 1em 0; }
  th, td { text-align: left; padding: .35em .5em; border-bottom: 1px solid #2a3140; }
  th { color: #9aa3b2; font-weight: 600; }
  .off { color: #c07070; }
  .on { color: #50c878; }
</style>
<h1>WifiHear</h1>
<p class="note">Passive 2.4 GHz survey on the authorized engagement.
This dashboard does not transmit. Deauth is not this C6 —
see docs/apps/airmaraud.md.</p>
<p>Scan: <strong id="scan" class="off">off</strong>
 · APs <span id="nap">0</span>
 · clients <span id="nsta">0</span></p>
<h2>Access points</h2>
<table id="aps"><thead><tr>
  <th>SSID</th><th>BSSID</th><th>ch</th><th>RSSI</th><th>auth</th>
</tr></thead><tbody></tbody></table>
<h2>Clients</h2>
<table id="stas"><thead><tr>
  <th>MAC</th><th>BSSID</th><th>RSSI</th>
</tr></thead><tbody></tbody></table>
<script>
async function tick(){
  const s = await (await fetch('/state')).json();
  const scan = document.getElementById('scan');
  scan.textContent = s.scan_on ? 'on' : 'off';
  scan.className = s.scan_on ? 'on' : 'off';
  document.getElementById('nap').textContent = s.aps.length;
  document.getElementById('nsta').textContent = s.stas.length;
  function esc(x){
    return String(x ?? '').replace(/[&<>"]/g, c =>
      ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
  }
  const apb = document.querySelector('#aps tbody');
  apb.innerHTML = s.aps.map(a =>
    `<tr><td>${esc(a.ssid)}</td><td>${esc(a.bssid)}</td><td>${esc(a.ch)}</td>`
    + `<td>${esc(a.rssi)}</td><td>${esc(a.auth)}</td></tr>`).join('');
  const stb = document.querySelector('#stas tbody');
  stb.innerHTML = s.stas.map(a =>
    `<tr><td>${esc(a.mac)}</td><td>${esc(a.bssid)}</td><td>${esc(a.rssi)}</td></tr>`).join('');
}
setInterval(tick, 500);
tick();
</script>
</html>
"""


class Survey:
    def __init__(self):
        self.lock = threading.Lock()
        self.scan_on = False
        self.aps = {}
        self.stas = {}

    def apply(self, ev):
        if not ev:
            return
        with self.lock:
            if ev["kind"] == "SCAN":
                self.scan_on = bool(ev["on"])
                if not self.scan_on:
                    self.aps.clear()
                    self.stas.clear()
            elif ev["kind"] == "AP":
                self.aps[ev["bssid"]] = ev
            elif ev["kind"] == "STA":
                self.stas[ev["mac"]] = ev

    def snapshot(self):
        with self.lock:
            aps = sorted(self.aps.values(),
                         key=lambda a: -(a.get("rssi") if a.get("rssi") is not None else -999))
            stas = sorted(self.stas.values(),
                          key=lambda a: -(a.get("rssi") or -999))
            return {
                "scan_on": self.scan_on,
                "aps": aps,
                "stas": stas,
            }


def reader_loop(ser, survey, stop):
    buf = ""
    while not stop.is_set():
        data = ser.read(4096)
        if not data:
            continue
        buf += data.decode("utf-8", "replace")
        while "\n" in buf:
            line, _, buf = buf.partition("\n")
            survey.apply(parse_wifihear_line(line.replace("\r", "")))


def make_handler(survey):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                body = INDEX_HTML.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path.startswith("/state"):
                body = json.dumps(survey.snapshot()).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            self.send_error(404)

    return Handler


def load_demo(survey):
    for line in (
        "WH on",
        "WH AP ssid=Lab-AP bssid=aabbccddeeff ch=6 rssi=-41 auth=wpa2",
        "WH AP ssid=Lab-IoT bssid=aabbccddee00 ch=1 rssi=-67 auth=wpa2",
        "WH STA mac=112233445566 bssid=aabbccddeeff rssi=-50",
    ):
        survey.apply(parse_wifihear_line(line))


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="wifihear",
        description="Serve a localhost HTML dashboard of WifiHear CDC scan "
                    "lines. Not a deauther. Never 1200 baud.",
    )
    p.add_argument("--port", help="COM port of the main CDC")
    p.add_argument("--bind", default="127.0.0.1:8765",
                   help="HTTP bind host:port (default 127.0.0.1:8765)")
    p.add_argument("--list", action="store_true",
                   help="print identified FreeWili CDC ports and exit")
    p.add_argument("--demo", action="store_true",
                   help="serve sample AP/STA rows without opening a serial port")
    args = p.parse_args(argv)

    survey = Survey()

    if args.list:
        try:
            ports = fw._cpu_ports()
        except ImportError:
            print("wifihear needs pyserial:  python -m pip install pyserial",
                  file=sys.stderr)
            return 1
        for p_ in ports:
            print(_fmt_port(p_))
        return 0

    ser = None
    stop = threading.Event()
    if args.demo:
        load_demo(survey)
        print("demo data loaded (no serial)", flush=True)
    else:
        try:
            fw.find_rpi_rp2()
        except fw.MultipleRp2VolumesError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        try:
            ports = fw._cpu_ports()
            picked = resolve_wifihear_port(ports, args.port)
            ser = open_cdc(picked.device)
        except (WifiHearError, fw.CpuPortError) as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except ImportError:
            print("wifihear needs pyserial:  python -m pip install pyserial",
                  file=sys.stderr)
            return 1
        t = threading.Thread(target=reader_loop, args=(ser, survey, stop),
                             daemon=True)
        t.start()
        print(f"reading {_fmt_port(picked)} at {CDC_BAUD} (not 1200)",
              flush=True)

    host, _, port_s = args.bind.partition(":")
    httpd = ThreadingHTTPServer((host, int(port_s or "8765")),
                                make_handler(survey))
    url = f"http://{host}:{httpd.server_address[1]}/"
    print(f"dashboard {url}  Ctrl-C to exit", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        httpd.server_close()
        if ser is not None:
            ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
