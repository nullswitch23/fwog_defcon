#!/usr/bin/env python3
"""Watch BleDeck's main CDC and Windows Bluetooth pairing.

FWOG-BleDeck can show up in the picker and still fail with "try connecting
your device again" — that is HOGP pairing, not the advertiser. This tool
prints the C6's `BN HID …` lines from the OG main CDC (conn/pair/enc/lock/
forget/bat/disc), lists the Windows Bluetooth device, and can forget a stuck
bond. Pairing itself is Windows Bluetooth settings; Gray hold on the board
wipes the C6 side.

Usage:
    python tools/bledeck.py              watch main CDC until Ctrl+C
    python tools/bledeck.py --win        list Bluetooth devices matching FWOG
    python tools/bledeck.py --forget     remove a Windows FWOG-BleDeck bond
    python tools/bledeck.py --port COM5  skip USB product identification
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time

DISC = {
    0x05: "auth failure (pair/enc failed)",
    0x08: "connection timeout",
    0x13: "remote user terminated",
    0x16: "connection terminated by local host",
    0x3E: "connection failed to be established",
    0x3D: "connection terminated due to MIC failure",
}


def _find_main_port():
    import pathlib
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import fw
    for p in fw._cpu_ports():
        if p.pid == fw.CPU_PID["main"]:
            return p.device
        prod = p.product or ""
        if p.vid in fw.FWOG_USB_VIDS and prod.startswith("FWOG main"):
            return p.device
    raise SystemExit("no FWOG main CDC (is bledeck_main enumerated?)")


def watch(port: str | None, seconds: float | None) -> int:
    import serial
    dev = port or _find_main_port()
    print("watching %s  (BN HID conn/pair/enc/disc)" % dev, file=sys.stderr)
    ser = serial.Serial(dev, 115200, timeout=0.2)
    ser.dtr = True
    time.sleep(0.3)
    ser.reset_input_buffer()
    deadline = None if seconds is None else time.monotonic() + seconds
    buf = ""
    try:
        while deadline is None or time.monotonic() < deadline:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", "replace")
            while "\n" in buf:
                line, _, buf = buf.partition("\n")
                line = line.strip()
                if not line:
                    continue
                extra = ""
                if "disc=0x" in line.lower() or "disc=" in line:
                    try:
                        hx = line.rsplit("=", 1)[-1].strip()
                        code = int(hx, 0)
                        extra = "  # " + DISC.get(code, "see Core Spec vol 2")
                    except ValueError:
                        extra = ""
                print(line + extra)
                sys.stdout.flush()
            time.sleep(0.02)
    except KeyboardInterrupt:
        print("", file=sys.stderr)
    finally:
        ser.close()
    return 0


def _ps(script: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["powershell", "-NoProfile", "-Command", script],
        capture_output=True,
        text=True,
    )


WIN_LIST = r"""
Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
  Where-Object { $_.FriendlyName -match 'FWOG|BleDeck' } |
  Select-Object Status, Problem, FriendlyName, InstanceId |
  Format-List | Out-String -Width 200
"""

WIN_FORGET = r"""
$devs = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
  Where-Object { $_.FriendlyName -match 'FWOG-BleDeck|BleDeck' }
if (-not $devs) { Write-Output 'NO_MATCH'; exit 2 }
foreach ($d in $devs) {
  Write-Output ("FORGET " + $d.FriendlyName + " " + $d.InstanceId)
  Disable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
  Remove-PnpDevice -InstanceId $d.InstanceId -Confirm:$false
}
"""


def win_list() -> int:
    r = _ps(WIN_LIST)
    out = (r.stdout or "") + (r.stderr or "")
    print(out.strip() or "(no FWOG/BleDeck Bluetooth device in PnP)")
    return 0 if r.returncode == 0 else r.returncode


def win_forget() -> int:
    r = _ps(WIN_FORGET)
    out = (r.stdout or "") + (r.stderr or "")
    print(out.strip() or r.returncode)
    if "NO_MATCH" in out:
        print("no Windows bond named FWOG-BleDeck; pair after the C6 rewrite",
              file=sys.stderr)
        return 2
    if r.returncode != 0:
        print("forget failed (try an elevated shell, or Settings > Bluetooth)",
              file=sys.stderr)
        return r.returncode
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", help="main CDC COM port (skip identification)")
    p.add_argument("--win", action="store_true",
                   help="list Windows Bluetooth devices matching FWOG")
    p.add_argument("--forget", action="store_true",
                   help="remove a stuck Windows FWOG-BleDeck bond")
    p.add_argument("--seconds", type=float, default=None,
                   help="watch this many seconds then exit (default: until Ctrl+C)")
    args = p.parse_args(argv)
    if args.forget:
        return win_forget()
    if args.win:
        return win_list()
    return watch(args.port, args.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
