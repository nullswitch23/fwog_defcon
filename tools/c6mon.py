#!/usr/bin/env python3
"""Reconnecting 115200 monitor for Bottlenose USB-Serial/JTAG.

ESP32-C6 RESET unplugs the COM port. pyserial miniterm dies with
ClearCommError; this loop waits and reopens. DTR/RTS stay low so opening
the port does not reset the chip again.

  python tools/c6mon.py
  python tools/c6mon.py --port COM9
"""
from __future__ import annotations

import argparse
import sys
import time

# Espressif USB-Serial/JTAG (C6). Not OG 093C, not FTDI 0403.
ESPRESSIF_VID = 0x303A


def find_port(explicit: str | None) -> str | None:
    from serial.tools import list_ports

    if explicit:
        want = explicit.upper()
        for p in list_ports.comports():
            if (p.device or "").upper() == want:
                return p.device
        return explicit
    hits = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(hits) == 1:
        return hits[0].device
    if not hits:
        return None
    print("several Espressif ports:", file=sys.stderr)
    for p in hits:
        print(f"  {p.device}  {p.description}", file=sys.stderr)
    print("pass --port COMx", file=sys.stderr)
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="COMx (default: the one Espressif 303A port)")
    args = ap.parse_args()
    try:
        import serial
    except ImportError:
        print("pip install pyserial", file=sys.stderr)
        return 1

    print("c6mon 115200 8N1 no flow; RESET is supposed to drop the port",
          file=sys.stderr)
    while True:
        port = find_port(args.port)
        if not port:
            time.sleep(0.2)
            continue
        print(f"-- {port} --", file=sys.stderr)
        ser = None
        try:
            ser = serial.Serial(port, 115200, timeout=0.2)
            ser.dtr = False
            ser.rts = False
            while True:
                n = ser.in_waiting
                data = ser.read(n or 1)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
        except serial.SerialException as e:
            print(f"\n-- USB dropped ({e.__class__.__name__}); waiting --",
                  file=sys.stderr)
        except KeyboardInterrupt:
            print(file=sys.stderr)
            return 0
        finally:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
        time.sleep(0.15)


if __name__ == "__main__":
    raise SystemExit(main())
