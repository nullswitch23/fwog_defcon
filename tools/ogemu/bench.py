#!/usr/bin/env python3
"""Stream world-bus JSONL into ogemu (--listen or stdout).

The LCD window is the device. This is the environment: accel, mic, RSSI, OOK.

  python tools/ogemu/ogemu.py --app hostdeck --listen 9320 --dump build-emu/hostdeck.png
  python tools/ogemu/bench.py --port 9320 --script tools/ogemu/scripts/world.jsonl

  python tools/ogemu/bench.py --port 9320 --rssi -72 --shake --mic 2400 --quit

Never open an RP2040 CDC at 1200 baud from this tool — it is not talking to
the board.
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import time


def accel_line(x: int, y: int, z: int) -> str:
    return json.dumps({"cmd": "accel", "x": x, "y": y, "z": z})


def shake_line(ms: int = 200) -> str:
    return json.dumps({"cmd": "shake", "ms": ms})


def mic_line(rms: int) -> str:
    return json.dumps({"cmd": "mic_rms", "rms": rms})


def rssi_line(dbm: int, hz: int = 433920000) -> str:
    return json.dumps({"cmd": "rssi", "dbm": dbm, "hz": hz})


def rf_line(art: int = 0, dbm: int = -72, hz: int = 433920000) -> str:
    return json.dumps({"cmd": "rf", "art": art, "dbm": dbm, "hz": hz})


def ook_line(path: str) -> str:
    return json.dumps({"cmd": "ook", "file": path})


def tick_line(ms: int) -> str:
    return json.dumps({"cmd": "tick", "ms": ms})


def press_line(btn: str) -> str:
    return json.dumps({"cmd": "press", "btn": btn})


def release_line(btn: str) -> str:
    return json.dumps({"cmd": "release", "btn": btn})


def dump_line() -> str:
    return json.dumps({"cmd": "dump"})


def quit_line() -> str:
    return json.dumps({"cmd": "quit"})


def split_inject_lines(text: str) -> list[str]:
    """JSONL or plain verbs; skip blanks and # // comments."""
    out: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        out.append(line)
    return out


def send_lines(sock: socket.socket | None, lines: list[str]) -> None:
    blob = "".join(line.rstrip("\n") + "\n" for line in lines if line.strip())
    if sock is None:
        sys.stdout.write(blob)
        sys.stdout.flush()
        return
    sock.sendall(blob.encode("ascii"))


def connect(host: str, port: int, timeout: float = 8.0) -> socket.socket:
    deadline = time.time() + timeout
    last: OSError | None = None
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=2.0)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            return s
        except OSError as e:
            last = e
            time.sleep(0.05)
    raise SystemExit(f"ogemu bench: connect {host}:{port} failed: {last}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="ogemu world-bus client")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, help="ogemu --listen PORT")
    p.add_argument("--script", help="JSONL file to stream")
    p.add_argument("--accel", help="x,y,z milli-g e.g. 0,0,1000")
    p.add_argument("--shake", nargs="?", const=200, type=int,
                   help="shake pulse (optional ms, default 200)")
    p.add_argument("--mic", type=int, help="mic RMS")
    p.add_argument("--rssi", type=int, help="dBm (negative)")
    p.add_argument("--hz", type=int, default=433920000)
    p.add_argument("--ook", help="OOK edge-timing file")
    p.add_argument("--tick", type=int, default=None,
                   help="virtual ms after injects (default 50 if injecting without --script)")
    p.add_argument("--quit", action="store_true", help="send quit after commands")
    p.add_argument("--repl", action="store_true",
                   help="read JSONL or verbs from stdin and forward")
    args = p.parse_args(argv)

    lines: list[str] = []
    if args.script:
        with open(args.script, encoding="utf-8") as f:
            lines.extend(x.rstrip("\n") for x in f if x.strip())
    if args.accel:
        parts = [int(x) for x in args.accel.split(",")]
        while len(parts) < 3:
            parts.append(1000 if len(parts) == 2 else 0)
        lines.append(accel_line(parts[0], parts[1], parts[2]))
    if args.shake is not None:
        lines.append(shake_line(args.shake))
    if args.mic is not None:
        lines.append(mic_line(args.mic))
    if args.rssi is not None:
        lines.append(rssi_line(args.rssi, args.hz))
    if args.ook:
        lines.append(ook_line(args.ook))
    injected = (args.accel or args.shake is not None or args.mic is not None
                or args.rssi is not None or args.ook)
    if args.tick is not None:
        lines.append(tick_line(args.tick))
    elif injected and not args.script:
        lines.append(tick_line(50))
    if args.quit:
        lines.append(quit_line())

    sock = connect(args.host, args.port) if args.port else None
    try:
        if lines:
            send_lines(sock, lines)
        if args.repl:
            if sock is None:
                raise SystemExit("ogemu bench: --repl needs --port")
            for raw in sys.stdin:
                raw = raw.strip()
                if not raw:
                    continue
                if raw[0] != "{":
                    # "rssi -72" / "accel 0 0 1000" / "quit"
                    raw = raw
                send_lines(sock, [raw])
                if raw in ("quit", '{"cmd": "quit"}') or '"cmd": "quit"' in raw:
                    break
    finally:
        if sock is not None:
            sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
