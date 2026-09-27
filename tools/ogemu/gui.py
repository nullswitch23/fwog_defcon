#!/usr/bin/env python3
"""Clickable FreeWili OG panel + JSONL world-bus box.

The LCD and five colour buttons are the device. Accel / mic / RSSI / OOK are
typed or sent as JSONL on the right — same protocol as bench.py / --listen.

  python tools/ogemu/ogemu.py --app hostdeck --gui

Never opens an RP2040 CDC (and never at 1200 baud).
"""
from __future__ import annotations

import argparse
import queue
import socket
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import scrolledtext, ttk
from pathlib import Path

import bench

# Hardware bitfield order: gray, yellow, green, blue, red.
BUTTONS = (
    ("gray",   "#8a8f98", "#1a1c20", "H / Up"),
    ("yellow", "#e6c229", "#2a2408", "Y / Left"),
    ("green",  "#3dcc7a", "#0d2818", "G / Enter"),
    ("blue",   "#4a8dff", "#0c1c38", "B / Right"),
    ("red",    "#e24b4b", "#2a0e0e", "R / Down"),
)

KEYMAP = {
    "g": "green", "G": "green", "Return": "green", "KP_Enter": "green",
    "y": "yellow", "Y": "yellow", "Left": "yellow",
    "b": "blue", "B": "blue", "Right": "blue",
    "h": "gray", "H": "gray", "Up": "gray",
    "r": "red", "R": "red", "Down": "red",
}

PRESETS = (
    ("rest",     '{"cmd": "accel", "x": 0, "y": 0, "z": 1000}'),
    ("shake",    '{"cmd": "shake", "ms": 200}'),
    ("mic 2400", '{"cmd": "mic_rms", "rms": 2400}'),
    ("mic 1kHz", '{"cmd": "mic_tone", "hz": 1000, "rms": 9000}'),
    ("rssi -72", '{"cmd": "rssi", "dbm": -72, "hz": 433920000}'),
    ("rf relic", '{"cmd": "rf", "art": 0, "dbm": -72, "hz": 433920000}'),
    ("bs 433",   '{"cmd": "bs_status", "band": 1, "peak_dbm": -48}'),
    ("am scan",  '{"cmd": "am_status", "hello": 1, "wp_on": 1, "aps": 1, "channel": 6, "log": "[WIFIPROOF] READY ch=6"}'),
    ("tf hunt",  '{"cmd": "tf_status", "rssi0": -80, "rssi1": -42}'),
    ("ook fan",  '{"cmd": "ook", "file": "tools/ogemu/scripts/fan.ook"}'),
    ("tick 50",  '{"cmd": "tick", "ms": 50}'),
)

BG = "#12141a"
FG = "#e6e8ee"
DIM = "#8b90a0"
PANEL_BG = "#080a12"
LCD_W, LCD_H = 320, 240


class OgEmuGui:
    def __init__(self, root: tk.Tk, *, app: str, port: int,
                 proc: subprocess.Popen, sock: socket.socket,
                 frame: Path, scale: int):
        self.root = root
        self.app = app
        self.port = port
        self.proc = proc
        self.sock = sock
        self.frame = frame
        self.scale = max(1, int(scale))
        self.log_q: queue.Queue[str] = queue.Queue()
        self.held: set[str] = set()
        self._photo: tk.PhotoImage | None = None
        self._photo_base: tk.PhotoImage | None = None
        self._mtime: float = -1.0
        self._closing = False

        root.title(f"ogemu {app} — panel is the device")
        root.configure(bg=BG)
        root.protocol("WM_DELETE_WINDOW", self.on_close)

        main = tk.Frame(root, bg=BG, padx=12, pady=10)
        main.pack(fill=tk.BOTH, expand=True)

        left = tk.Frame(main, bg=BG)
        left.pack(side=tk.LEFT, fill=tk.Y)
        right = tk.Frame(main, bg=BG)
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(16, 0))

        tk.Label(left, text="LCD  320×240", fg=DIM, bg=BG,
                 font=("Segoe UI", 9)).pack(anchor="w")
        self.canvas = tk.Canvas(
            left, width=LCD_W * self.scale, height=LCD_H * self.scale,
            bg=PANEL_BG, highlightthickness=1, highlightbackground="#2a2e3a",
            cursor="dot")
        self.canvas.pack()
        self.canvas.create_text(
            LCD_W * self.scale // 2, LCD_H * self.scale // 2,
            text="waiting for frame…", fill=DIM, font=("Segoe UI", 11))

        tk.Label(left, text="Buttons  (hold = mouse down)", fg=DIM, bg=BG,
                 font=("Segoe UI", 9)).pack(anchor="w", pady=(10, 4))
        row = tk.Frame(left, bg=BG)
        row.pack()
        self.btn_widgets: dict[str, tk.Button] = {}
        for name, fill, down, hint in BUTTONS:
            b = tk.Button(
                row, text=name.upper(), bg=fill, fg="#111",
                activebackground=fill, relief=tk.RAISED, bd=2,
                width=8, height=2, font=("Segoe UI", 9, "bold"),
                cursor="hand2")
            b.pack(side=tk.LEFT, padx=3)
            b.bind("<ButtonPress-1>", lambda e, n=name: self.btn_down(n))
            b.bind("<ButtonRelease-1>", lambda e, n=name: self.btn_up(n))
            b.bind("<Leave>", lambda e, n=name: self.btn_up(n))
            b._ogemu_fill = fill  # type: ignore[attr-defined]
            b._ogemu_down = down  # type: ignore[attr-defined]
            self.btn_widgets[name] = b
        hint = "  ·  ".join(f"{n} {h}" for n, _, _, h in BUTTONS)
        tk.Label(left, text=hint, fg=DIM, bg=BG,
                 font=("Segoe UI", 8)).pack(anchor="w", pady=(6, 0))

        tk.Label(right, text="World bus  JSONL", fg=DIM, bg=BG,
                 font=("Segoe UI", 9)).pack(anchor="w")
        self.verbs = tk.Text(
            right, height=8, bg="#1a1d26", fg=FG, insertbackground=FG,
            relief=tk.FLAT, font=("Consolas", 10), wrap=tk.NONE)
        self.verbs.pack(fill=tk.BOTH, expand=False)
        self.verbs.insert("1.0", '{"cmd": "shake", "ms": 200}\n')
        self.verbs.bind("<Control-Return>", lambda e: (self.send_box(), "break")[1])

        presets = tk.Frame(right, bg=BG)
        presets.pack(fill=tk.X, pady=(6, 0))
        for label, line in PRESETS:
            ttk.Button(presets, text=label,
                       command=lambda ln=line: self.send_lines([ln])).pack(
                side=tk.LEFT, padx=2)

        send_row = tk.Frame(right, bg=BG)
        send_row.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(send_row, text="Send JSONL", command=self.send_box).pack(
            side=tk.LEFT)
        tk.Label(send_row, text="Ctrl+Enter", fg=DIM, bg=BG,
                 font=("Segoe UI", 8)).pack(side=tk.LEFT, padx=8)

        tk.Label(right, text="DIAG", fg=DIM, bg=BG,
                 font=("Segoe UI", 9)).pack(anchor="w", pady=(12, 2))
        self.log = scrolledtext.ScrolledText(
            right, height=16, bg="#0e1016", fg="#b8c0ce",
            relief=tk.FLAT, font=("Consolas", 9), state=tk.DISABLED)
        self.log.pack(fill=tk.BOTH, expand=True)

        self.status = tk.Label(
            root, text=f"127.0.0.1:{port}  ·  G/Y/B/H/R or arrows  ·  close window to quit",
            fg=DIM, bg="#0c0e14", anchor="w", padx=12, pady=4,
            font=("Segoe UI", 8))
        self.status.pack(fill=tk.X, side=tk.BOTTOM)

        root.bind_all("<KeyPress>", self.on_key_press)
        root.bind_all("<KeyRelease>", self.on_key_release)

        threading.Thread(target=self._read_stdout, daemon=True).start()
        self.root.after(40, self._tick)

    def _read_stdout(self) -> None:
        if self.proc.stdout is None:
            return
        for line in self.proc.stdout:
            self.log_q.put(line.rstrip("\n"))

    def _append_log(self, line: str) -> None:
        self.log.configure(state=tk.NORMAL)
        self.log.insert(tk.END, line + "\n")
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)

    def send_lines(self, lines: list[str]) -> None:
        if self._closing:
            return
        cleaned = bench.split_inject_lines("\n".join(lines))
        if not cleaned:
            return
        try:
            bench.send_lines(self.sock, cleaned)
        except OSError as e:
            self._append_log(f"gui: send failed: {e}")
            return
        for ln in cleaned:
            self._append_log(f"> {ln}")

    def send_box(self) -> None:
        self.send_lines(bench.split_inject_lines(self.verbs.get("1.0", tk.END)))

    def btn_down(self, name: str) -> None:
        if name in self.held:
            return
        self.held.add(name)
        w = self.btn_widgets.get(name)
        if w is not None:
            w.configure(relief=tk.SUNKEN, bg=getattr(w, "_ogemu_down", "#333"))
        self.send_lines([bench.press_line(name)])

    def btn_up(self, name: str) -> None:
        if name not in self.held:
            return
        self.held.discard(name)
        w = self.btn_widgets.get(name)
        if w is not None:
            w.configure(relief=tk.RAISED, bg=getattr(w, "_ogemu_fill", "#888"))
        self.send_lines([bench.release_line(name)])

    def _focus_is_text(self) -> bool:
        w = self.root.focus_get()
        return isinstance(w, (tk.Text, tk.Entry, ttk.Entry))

    def on_key_press(self, event: tk.Event) -> None:
        if self._focus_is_text():
            return
        name = KEYMAP.get(event.keysym)
        if name:
            self.btn_down(name)

    def on_key_release(self, event: tk.Event) -> None:
        name = KEYMAP.get(event.keysym)
        if name:
            self.btn_up(name)

    def _reload_frame(self) -> None:
        if not self.frame.is_file():
            return
        try:
            mtime = self.frame.stat().st_mtime
            if mtime == self._mtime:
                return
            raw = self.frame.read_bytes()
        except OSError:
            return
        if len(raw) < 16 or not raw.startswith(b"P6"):
            return
        try:
            img = tk.PhotoImage(data=raw.decode("latin-1"))
            shown = img.zoom(self.scale, self.scale) if self.scale > 1 else img
        except tk.TclError:
            return
        self._photo_base = img
        self._photo = shown
        self._mtime = mtime
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, image=shown, anchor="nw")

    def _tick(self) -> None:
        if self._closing:
            return
        try:
            while True:
                self._append_log(self.log_q.get_nowait())
        except queue.Empty:
            pass
        self._reload_frame()
        rc = self.proc.poll()
        if rc is not None:
            self.status.configure(text=f"ogemu exited {rc}")
            self._append_log(f"gui: ogemu exited {rc}")
            return
        self.root.after(40, self._tick)

    def on_close(self) -> None:
        if self._closing:
            return
        self._closing = True
        try:
            bench.send_lines(self.sock, [bench.quit_line()])
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=2.5)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
        self.root.destroy()


def launch_emu(app: str, port: int, frame: Path, dump: Path,
               no_build: bool) -> subprocess.Popen:
    import ogemu as og
    binary = og.exe_name(app) if no_build else og.configure_and_build(app)
    if no_build and not binary.is_file():
        found = list(og.BUILD.rglob(f"ogemu_{app}*"))
        if not found:
            raise SystemExit(f"ogemu gui: {binary} missing; drop --no-build")
        binary = found[0]
    dump.parent.mkdir(parents=True, exist_ok=True)
    frame.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(binary), "--listen", str(port),
           "--frame", str(frame), "--dump", str(dump)]
    print("+", " ".join(cmd), flush=True)
    return subprocess.Popen(
        cmd, cwd=str(og.REPO), env=og.host_env(),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1)


def run_gui(app: str = "hostdeck", port: int = 9320, no_build: bool = False,
            dump: str | None = None, frame: str | None = None,
            scale: int = 2) -> int:
    import ogemu as og
    dump_path = Path(dump) if dump else og.BUILD / f"{app}.png"
    frame_path = Path(frame) if frame else og.BUILD / f"{app}.ppm"
    proc = launch_emu(app, port, frame_path, dump_path, no_build)
    try:
        sock = bench.connect("127.0.0.1", port, timeout=12.0)
    except SystemExit:
        proc.terminate()
        raise
    root = tk.Tk()
    OgEmuGui(root, app=app, port=port, proc=proc, sock=sock,
             frame=frame_path, scale=scale)
    root.mainloop()
    if proc.poll() is None:
        proc.terminate()
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="ogemu clickable panel + JSONL")
    p.add_argument("--app", default="hostdeck")
    p.add_argument("--port", type=int, default=9320)
    p.add_argument("--scale", type=int, default=2)
    p.add_argument("--dump", help="PNG written on quit")
    p.add_argument("--frame", help="live P6 PPM path")
    p.add_argument("--no-build", action="store_true")
    args = p.parse_args(argv)
    return run_gui(app=args.app, port=args.port, no_build=args.no_build,
                   dump=args.dump, frame=args.frame, scale=args.scale)


if __name__ == "__main__":
    sys.exit(main())
