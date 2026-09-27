#!/usr/bin/env python3
"""Rebuild a GlassBak FatFs dump from main USB CDC, and push it back.

GlassBak / KitHome GlassBak tile prints:

    FSBK1
    FILE /OPTIC.BIN 772
    aabbcc...
    END

This helper writes those files under --out, grouped by the app that owns
the FatFs path. After a dump, TalkClip ``.RAW`` files (DGF1 + 8 kHz
int16 LE PCM) also get a sibling ``.wav`` via ffmpeg (or Python ``wave``
if ffmpeg is missing). Push sends PUT /path size plus hex so firmware
recreates the same volume paths the original apps open. Host ``.wav``
sidecars are not pushed.

Never 1200 baud (BOOTSEL). Dump and restore live on **main** CDC.
"""
from __future__ import annotations

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import time
import wave

_HERE = pathlib.Path(__file__).resolve().parent
_TOOLS = _HERE.parent
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

import fw  # noqa: E402

CDC_BAUD = 115200
BOOTSEL_BAUD = 1200
DG_MAGIC = 0x31474644  # 'DGF1'
DG_KIND_PCM16 = 1
DG_HDR_N = 48

# First matching prefix wins. Paths are FatFs as dumped (no leading slash
# after parse_dump writes the host tree).
_APP_PREFIXES = (
    ("chirpmail/", "ChirpMail"),
    ("talkclip/", "TalkClip"),
    ("trailrf/", "InertialTrailRF"),
    ("trail/", "InertialTrail"),
    ("ismburst/", "ISMburst"),
    ("fobreplay/", "FobReplay"),
    ("scripts/", "DiskGlass"),
)

_APP_FILES = {
    "optic.bin": "OpticClick",
    "phlib.bin": "PingHalo",
    "mscope.cal": "MicScope",
    "voltpet.bin": "VoltPet",
    "ismburst.bin": "ISMburst",
}


def classify(rel: str) -> str:
    """Map a FatFs-relative path to the app that opens it."""
    p = rel.replace("\\", "/").lstrip("/").lower()
    if p in _APP_FILES:
        return _APP_FILES[p]
    for prefix, app in _APP_PREFIXES:
        if p.startswith(prefix):
            return app
    return "other"


def fatfs_path(rel: str) -> str:
    """Host-relative path → the '/' path firmware apps open."""
    p = rel.replace("\\", "/").lstrip("/")
    return "/" + p


def group_tree(root: pathlib.Path) -> dict[str, list[pathlib.Path]]:
    groups: dict[str, list[pathlib.Path]] = {}
    if not root.exists():
        return groups
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        groups.setdefault(classify(rel), []).append(p)
    return groups


def _as_cpu_port(ports, picked):
    """fw._pick_cpu_port returns a device string, not a CpuPort."""
    if hasattr(picked, "device"):
        return picked
    name = str(picked)
    for p in ports:
        if (p.device or "").upper() == name.upper():
            return p
    raise SystemExit(f"port {name} not found")


def _pick_main(port_name=None):
    ports = fw._cpu_ports()
    if port_name:
        return _as_cpu_port(ports, port_name)
    try:
        picked = fw._pick_cpu_port(ports, "main")
    except fw.CpuPortError as e:
        raise SystemExit(str(e)) from e
    return _as_cpu_port(ports, picked)


def _refuse_display(target) -> None:
    if getattr(target, "pid", None) == fw.CPU_PID["display"]:
        raise SystemExit("refusing display CDC — GlassBak dumps on main")
    prod = (getattr(target, "product", None) or "").lower()
    if "display" in prod and "main" not in prod:
        raise SystemExit("refusing display CDC — GlassBak dumps on main")


def pcm16_from_dgf1(data: bytes):
    """TalkClip / DiskGlass PCM file: 48-byte DGF1 header then s16le samples.

    Returns (pcm, sample_hz) or None.
    """
    if len(data) < DG_HDR_N:
        return None
    magic, _ver, kind, payload_bytes, sample_hz = struct.unpack_from(
        "<IHHII", data, 0)
    if magic != DG_MAGIC or kind != DG_KIND_PCM16:
        return None
    pcm = data[DG_HDR_N:]
    if payload_bytes and len(pcm) > payload_bytes:
        pcm = pcm[:payload_bytes]
    if len(pcm) % 2:
        pcm = pcm[:-1]
    hz = int(sample_hz) if sample_hz else 8000
    if hz <= 0:
        hz = 8000
    return pcm, hz


def ffmpeg_pcm_cmd(ffmpeg: str, hz: int, dest: pathlib.Path) -> list[str]:
    return [
        ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
        "-f", "s16le", "-ar", str(hz), "-ac", "1", "-i", "pipe:0",
        str(dest),
    ]


def write_wav(pcm: bytes, hz: int, dest: pathlib.Path) -> str:
    """Write mono s16le WAV. Prefer ffmpeg; fall back to the stdlib."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    ff = shutil.which("ffmpeg")
    if ff:
        r = subprocess.run(
            ffmpeg_pcm_cmd(ff, hz, dest),
            input=pcm, capture_output=True, check=False)
        if r.returncode == 0 and dest.is_file() and dest.stat().st_size > 44:
            return "ffmpeg"
    with wave.open(str(dest), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(hz)
        w.writeframes(pcm)
    return "wave"


def export_talkclip_wavs(root: pathlib.Path, log=print) -> int:
    """Sibling .wav next to each DGF1 PCM .RAW under root. Leaves .RAW."""
    n = 0
    if not root.exists():
        return 0
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix.lower() != ".raw":
            continue
        parsed = pcm16_from_dgf1(p.read_bytes())
        if not parsed:
            continue
        pcm, hz = parsed
        wav = p.with_suffix(".wav")
        how = write_wav(pcm, hz, wav)
        n += 1
        log(f"{wav.name} ({how}, {hz} Hz, {len(pcm)} bytes pcm)")
    return n


def _unhex(line: str) -> bytes:
    s = "".join(line.split())
    if not s:
        return b""
    if len(s) % 2:
        raise ValueError("odd hex length")
    return bytes.fromhex(s)


def parse_dump(lines, out_dir: pathlib.Path) -> int:
    """Write FILE records from an iterator of text lines. Returns file count."""
    nfiles = 0
    dest = None
    size = 0
    got = 0
    started = False
    for raw in lines:
        line = raw.strip()
        if not started:
            if line == "FSBK1":
                started = True
            continue
        if line == "END":
            break
        if line.startswith("#"):
            continue
        if line.startswith("FILE "):
            parts = line.split()
            if len(parts) < 3:
                continue
            rel = parts[1].lstrip("/")
            size = int(parts[2])
            dest = out_dir / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(b"")
            got = 0
            nfiles += 1
            continue
        if dest is None:
            continue
        chunk = _unhex(line)
        with dest.open("ab") as f:
            f.write(chunk)
        got += len(chunk)
        if size and got >= size:
            dest = None
    return nfiles


def format_put(path: str, data: bytes) -> str:
    """CDC text firmware gb_fs_poll_restore() consumes."""
    lines = [f"PUT {path} {len(data)}"]
    for i in range(0, len(data), 32):
        lines.append(data[i : i + 32].hex())
    lines.append("END")
    return "\n".join(lines) + "\n"


def pull(out: pathlib.Path, port=None, timeout: float = 120.0, log=print) -> int:
    out.mkdir(parents=True, exist_ok=True)
    target = _pick_main(port)
    _refuse_display(target)
    import serial

    log(f"open {target.device} ({target.product}) at {CDC_BAUD}, wait for FSBK1")
    log("press GREEN on the OG (GlassBak or KitHome GlassBak tile)")
    ser = serial.Serial(target.device, CDC_BAUD, timeout=0.2)
    try:
        deadline = time.time() + timeout
        buf = ""
        started = False
        acc = []
        while time.time() < deadline or started:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk.decode("ascii", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip("\r")
                    if not started:
                        if line.strip() == "FSBK1":
                            started = True
                            acc.append("FSBK1\n")
                            log("dump started")
                        continue
                    acc.append(line + "\n")
                    if line.strip().startswith("FILE "):
                        log(line.strip())
                    if line.strip() == "END":
                        n = parse_dump(acc, out)
                        log(f"wrote {n} files under {out.resolve()}")
                        nw = export_talkclip_wavs(out, log=log)
                        if nw:
                            log(f"wrote {nw} TalkClip wav sidecar(s)")
                        return n
            elif started:
                deadline = time.time() + 30.0
        raise SystemExit("timeout waiting for dump (is GlassBak/KitHome flashed? Green?)")
    finally:
        ser.close()


def push_files(files: list[tuple[str, bytes]], port=None, log=print) -> int:
    if not files:
        log("nothing to push")
        return 0
    target = _pick_main(port)
    _refuse_display(target)
    import serial

    log(f"push {len(files)} file(s) to {target.device} ({target.product})")
    ser = serial.Serial(target.device, CDC_BAUD, timeout=0.2)
    try:
        for path, data in files:
            log(f"PUT {path} {len(data)}")
            ser.write(format_put(path, data).encode("ascii"))
            ser.flush()
            time.sleep(0.05)
        return len(files)
    finally:
        ser.close()


def collect_push(root: pathlib.Path, only_app: str | None = None) -> list[tuple[str, bytes]]:
    out = []
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.suffix.lower() == ".wav":
            continue
        rel = p.relative_to(root).as_posix()
        app = classify(rel)
        if only_app and app.lower() != only_app.lower():
            continue
        out.append((fatfs_path(rel), p.read_bytes()))
    return out


def run_gui(out_dir: pathlib.Path, port=None) -> int:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    root = tk.Tk()
    root.title("fsbak — FatFs by app")
    root.geometry("720x480")

    tree = ttk.Treeview(root, columns=("path", "bytes"), show="tree headings")
    tree.heading("#0", text="app")
    tree.heading("path", text="FatFs path")
    tree.heading("bytes", text="bytes")
    tree.column("#0", width=140)
    tree.column("path", width=360)
    tree.column("bytes", width=80)
    logbox = tk.Text(root, height=8, wrap="word")

    def log(msg: str) -> None:
        logbox.insert("end", msg + "\n")
        logbox.see("end")
        root.update_idletasks()

    def refresh() -> None:
        tree.delete(*tree.get_children())
        groups = group_tree(out_dir)
        for app in sorted(groups):
            parent = tree.insert("", "end", text=app, values=("", ""))
            for p in groups[app]:
                rel = p.relative_to(out_dir).as_posix()
                tree.insert(parent, "end", text="",
                            values=(fatfs_path(rel), str(p.stat().st_size)),
                            tags=(app,))
            tree.item(parent, open=True)

    def do_pull() -> None:
        try:
            pull(out_dir, port=port, log=log)
            refresh()
        except SystemExit as e:
            messagebox.showerror("fsbak", str(e))
        except Exception as e:
            messagebox.showerror("fsbak", str(e))

    def selected_files() -> list[tuple[str, bytes]]:
        items = tree.selection()
        files = []
        seen = set()
        for iid in items:
            kids = tree.get_children(iid)
            if kids:
                for k in kids:
                    path = tree.set(k, "path")
                    if path and path not in seen and not path.lower().endswith(".wav"):
                        host = out_dir / path.lstrip("/")
                        if host.is_file():
                            files.append((path, host.read_bytes()))
                            seen.add(path)
            else:
                path = tree.set(iid, "path")
                if path and path not in seen and not path.lower().endswith(".wav"):
                    host = out_dir / path.lstrip("/")
                    if host.is_file():
                        files.append((path, host.read_bytes()))
                        seen.add(path)
        if not files:
            return collect_push(out_dir)
        return files

    def do_push() -> None:
        try:
            n = push_files(selected_files(), port=port, log=log)
            log(f"pushed {n} file(s) to original FatFs paths")
        except SystemExit as e:
            messagebox.showerror("fsbak", str(e))
        except Exception as e:
            messagebox.showerror("fsbak", str(e))

    def do_browse() -> None:
        nonlocal out_dir
        d = filedialog.askdirectory(initialdir=str(out_dir))
        if d:
            out_dir = pathlib.Path(d)
            folder.set(str(out_dir))
            refresh()

    bar = ttk.Frame(root)
    ttk.Button(bar, text="Pull (wait, then Green on OG)", command=do_pull).pack(side="left", padx=4)
    ttk.Button(bar, text="Push selected / all", command=do_push).pack(side="left", padx=4)
    ttk.Button(bar, text="Folder…", command=do_browse).pack(side="left", padx=4)
    folder = tk.StringVar(value=str(out_dir.resolve()))
    ttk.Label(bar, textvariable=folder).pack(side="left", padx=8)

    bar.pack(fill="x", pady=6)
    tree.pack(fill="both", expand=True, padx=6)
    logbox.pack(fill="x", padx=6, pady=6)
    ttk.Label(root, text="Push writes PUT /path size + hex on main CDC. Never 1200 baud.").pack(anchor="w", padx=8, pady=4)
    refresh()
    root.mainloop()
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="fsbak")
    p.add_argument("--out", default="fsbak-out", help="host directory (mirrors FatFs)")
    p.add_argument("--port", default=None)
    p.add_argument("--list", action="store_true")
    p.add_argument("--gui", action="store_true", help="tkinter: files grouped by app")
    p.add_argument("--push", action="store_true",
                   help="write --out tree back to the OG at original paths")
    p.add_argument("--app", default=None, help="with --push, only this classify() group")
    p.add_argument("--timeout", type=float, default=120.0,
                   help="seconds to wait for FSBK1 after open")
    args = p.parse_args(argv)

    if args.list:
        for cp in fw._cpu_ports():
            print(f"{cp.device}\tpid={cp.pid}\t{cp.product}")
        return 0

    if args.port and str(args.port).strip() == str(BOOTSEL_BAUD):
        raise SystemExit("refusing 1200 baud")

    out = pathlib.Path(args.out)
    if args.gui:
        return run_gui(out, port=args.port)
    if args.push:
        files = collect_push(out, only_app=args.app)
        n = push_files(files, port=args.port)
        print(f"pushed {n} file(s)")
        return 0

    pull(out, port=args.port, timeout=args.timeout)
    groups = group_tree(out)
    for app, paths in sorted(groups.items()):
        print(f"{app}: {len(paths)} file(s)")
        for fp in paths:
            print(f"  {fatfs_path(fp.relative_to(out).as_posix())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
