#!/usr/bin/env python3
"""Compose up to 24 ChirpMail canned lines and push them to the OG.

The OG stores them in FatFs `/chirpmail/CANNED.TXT`. T9 on the device
overrides a slot (Gray hold, Green hold to save). This helper is the
PC side: edit 24 x 20-character lines, then push over **main** USB CDC.

    python tools/chirpmail/chirpmail.py
    python tools/chirpmail/chirpmail.py --file canned.txt push
    python tools/chirpmail/chirpmail.py list

Never 1200 baud (BOOTSEL). Main CDC only (`FWOG main chirpmail …`).
"""
from __future__ import annotations

import argparse
import pathlib
import sys
import time

_HERE = pathlib.Path(__file__).resolve().parent
_TOOLS = _HERE.parent
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

import fw  # noqa: E402

CDC_BAUD = 115200
BOOTSEL_BAUD = 1200
NSLOT = 24
NCHAR = 20
DEFAULT_FILE = _HERE / "canned.txt"


def sanitize(s: str) -> str:
    out = []
    for c in (s or "").replace("\t", " "):
        if c in "\r\n":
            break
        o = ord(c)
        if 0x20 <= o <= 0x7E:
            out.append(c)
        if len(out) >= NCHAR:
            break
    return "".join(out)


def parse_canned(text: str) -> list[str]:
    slots = [""] * NSLOT
    i = 0
    for raw in text.splitlines():
        line = raw.strip("\n")
        if line.startswith("#"):
            continue
        if i >= NSLOT:
            break
        slots[i] = sanitize(line)
        i += 1
    return slots


def format_canned(slots: list[str]) -> str:
    lines = [
        "# ChirpMail canned — 24 lines, 20 characters each.",
        "# Push with: python tools/chirpmail/chirpmail.py --file this.txt push",
    ]
    body = list(slots[:NSLOT]) + [""] * NSLOT
    lines.extend(sanitize(s) for s in body[:NSLOT])
    return "\n".join(lines) + "\n"


def wire_script(slots: list[str]) -> str:
    out = ["CM1"]
    for i, s in enumerate(slots[:NSLOT]):
        t = sanitize(s)
        out.append(f"SLOT {i:02d} {t}".rstrip())
    out.append("END")
    return "\n".join(out) + "\n"


def load_file(path: pathlib.Path) -> list[str]:
    if not path.exists():
        return parse_canned("")
    return parse_canned(path.read_text(encoding="utf-8"))


def _as_cpu_port(ports, picked):
    """fw._pick_cpu_port returns a device string, not a CpuPort."""
    if hasattr(picked, "device"):
        return picked
    name = str(picked)
    for p in ports:
        if (p.device or "").upper() == name.upper():
            return p
    raise SystemExit(f"port {name} not found")


def _pick_chirpmail_main(port_name=None):
    ports = fw._cpu_ports()
    if port_name:
        return _as_cpu_port(ports, port_name)
    mains = []
    for p in ports:
        prod = (p.product or "").lower()
        if "chirpmail" in prod and "main" in prod:
            mains.append(p)
    if len(mains) == 1:
        return mains[0]
    try:
        picked = fw._pick_cpu_port(ports, "main")
    except fw.CpuPortError as e:
        raise SystemExit(str(e) + " — flash chirpmail_main first") from e
    cand = _as_cpu_port(ports, picked)
    if "chirpmail" not in (cand.product or "").lower():
        raise SystemExit(
            f"{cand.device} is main CDC but not ChirpMail "
            f"({cand.product or '?'}). Flash chirpmail_main."
        )
    return cand


def push(slots: list[str], *, port=None) -> None:
    target = _pick_chirpmail_main(port)
    prod = (target.product or "").lower()
    if "display" in prod and "main" not in prod:
        raise SystemExit("refusing display CDC — canned push is on main")
    try:
        import serial
    except ImportError as e:
        raise SystemExit("pyserial required: python -m pip install pyserial") from e
    script = wire_script(slots)
    print(f"open {target.device} ({target.product}) at {CDC_BAUD}")
    ser = serial.Serial(target.device, CDC_BAUD, timeout=0.2)
    try:
        time.sleep(0.2)
        ser.write(script.encode("ascii", "replace"))
        ser.flush()
        time.sleep(0.4)
        leftover = ser.read(4096).decode("ascii", "replace")
        if leftover.strip():
            print(leftover.strip())
    finally:
        ser.close()
    print("pushed 24 slots")


def run_gui(path: pathlib.Path) -> int:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    slots = load_file(path)
    root = tk.Tk()
    root.title("ChirpMail canned (24 × 20)")
    frm = ttk.Frame(root, padding=8)
    frm.pack(fill="both", expand=True)
    entries: list[tk.Entry] = []
    for i in range(NSLOT):
        row = ttk.Frame(frm)
        row.pack(fill="x", pady=1)
        ttk.Label(row, text=f"{i:02d}", width=3).pack(side="left")
        e = tk.Entry(row, width=NCHAR + 2)
        e.insert(0, slots[i])
        e.pack(side="left", fill="x", expand=True)
        entries.append(e)

    def collect() -> list[str]:
        return [sanitize(e.get()) for e in entries]

    def do_save():
        dest = filedialog.asksaveasfilename(
            defaultextension=".txt", initialfile=path.name)
        if not dest:
            return
        pathlib.Path(dest).write_text(format_canned(collect()), encoding="utf-8")

    def do_open():
        src = filedialog.askopenfilename(filetypes=[("Text", "*.txt"), ("All", "*")])
        if not src:
            return
        got = load_file(pathlib.Path(src))
        for e, s in zip(entries, got):
            e.delete(0, "end")
            e.insert(0, s)

    def do_push():
        try:
            push(collect())
            messagebox.showinfo("ChirpMail", "Pushed 24 slots to main CDC")
        except SystemExit as ex:
            messagebox.showerror("ChirpMail", str(ex))
        except Exception as ex:
            messagebox.showerror("ChirpMail", str(ex))

    bar = ttk.Frame(frm)
    bar.pack(fill="x", pady=8)
    ttk.Button(bar, text="Open", command=do_open).pack(side="left", padx=2)
    ttk.Button(bar, text="Save", command=do_save).pack(side="left", padx=2)
    ttk.Button(bar, text="Push to OG", command=do_push).pack(side="left", padx=2)
    ttk.Label(frm, text="Main CDC only. Never 1200 baud. T9 on the OG still overrides a slot.").pack(
        anchor="w")
    root.mainloop()
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--file", type=pathlib.Path, default=DEFAULT_FILE)
    p.add_argument("--port")
    p.add_argument("cmd", nargs="?", default="gui",
                   choices=("gui", "push", "list", "wire"))
    args = p.parse_args(argv)
    slots = load_file(args.file)
    if args.cmd == "list":
        for i, s in enumerate(slots):
            print(f"{i:02d} {s}")
        return 0
    if args.cmd == "wire":
        sys.stdout.write(wire_script(slots))
        return 0
    if args.cmd == "push":
        if args.file.exists():
            args.file.write_text(format_canned(slots), encoding="utf-8")
        push(slots, port=args.port)
        return 0
    return run_gui(args.file)


if __name__ == "__main__":
    raise SystemExit(main())
